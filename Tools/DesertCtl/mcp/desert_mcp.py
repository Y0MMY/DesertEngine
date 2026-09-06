#!/usr/bin/env python3
"""A thin MCP server over the Desert editor's control channel.

THE CHANNEL IS PRIMARY AND THIS IS A SHIM. Everything below is a translation of an MCP tool call into one
`desertctl` invocation and back; there is no state here, no retry policy, and no vocabulary of its own. That
is deliberate, and it is the decision the channel was designed around: the channel has to be useful to CI
and to headless verification runs that have never heard of MCP, so nothing in it may be shaped by this file.
If a tool here ever wants the channel to behave differently, the answer is to say so rather than to bend the
channel — the channel is the contract, this is one of its clients.

WHAT THE TOOLS ARE. They are not a curated list of editor features: `run_command` executes whatever the
editor's own COMMAND PALETTE offers this instant, and `list_commands` is how an agent finds out what that
is. So a capability added to the editor for a person appears here with nobody touching this file, which is
the same property that made the channel worth building in the first place.

WHY THE SHOTS ARE TWO TOOLS AND NOT ONE WITH A FLAG. `capture_window` reads the presented frame and contains
the panels, the menus and the dialogs; `capture_viewport` reads the scene's own image and contains none of
them. Before this channel existed only the second was possible at all, which is why no picture this engine
ever took held a pixel of its interface. A single tool that silently answered one with the other would
deliver a picture of the wrong subject under the right name.

Usage:
    desert_mcp.py --socket /tmp/desert-editor.sock [--desertctl path/to/DesertCtl]

Speaks MCP over stdio: one JSON-RPC message per line.
"""

import argparse
import json
import os
import subprocess
import sys

PROTOCOL_VERSION = "2024-11-05"


class ChannelError(RuntimeError):
    """The editor refused, or could not be reached. Carried separately from a tool's own bad arguments,
    because the two need different fixes: one is the editor's answer, the other is this client's mistake."""


class Channel:
    """One `desertctl` invocation per call. No connection is held between calls, and that is not a
    simplification — the editor answers a command only after a frame that already reflects it, so the reply
    IS the synchronisation, and a held connection would buy nothing but a state to get wrong."""

    def __init__(self, binary: str, socket_path: str):
        self.binary = binary
        self.socket_path = socket_path

    def call(self, *args: str, wait: float = 0.0) -> dict:
        command = [self.binary, "--socket", self.socket_path]
        if wait:
            command += ["--wait", str(wait)]
        command += list(args)

        try:
            done = subprocess.run(command, capture_output=True, text=True, timeout=300)
        except FileNotFoundError as exc:
            raise ChannelError(
                f"{self.binary} is not there. Build it: make DesertCtl config=debug"
            ) from exc
        except subprocess.TimeoutExpired as exc:
            raise ChannelError(
                "the editor did not answer within 300 s. It is wedged, or a command is waiting for a "
                "frame that will never settle."
            ) from exc

        # Exit code 2 means the tool never reached an editor — a different thing from a refusal, and worth
        # saying so rather than reporting an empty reply.
        if done.returncode == 2:
            raise ChannelError(done.stderr.strip() or "no editor is listening on that socket.")

        line = done.stdout.strip()
        if not line:
            raise ChannelError(done.stderr.strip() or "the editor answered with nothing at all.")

        try:
            reply = json.loads(line)
        except json.JSONDecodeError as exc:
            raise ChannelError(f"the editor's reply is not JSON: {line!r}") from exc

        # The outcome is read from the parsed reply, never guessed from the exit code alone: a reply that
        # carries no outcome is a defect in the channel and must not be reported as success.
        if "ok" not in reply:
            raise ChannelError("the reply carries no outcome; that is a defect in the channel.")
        if not reply["ok"]:
            raise ChannelError(reply.get("error", "refused, with no reason given."))
        return reply


TOOLS = [
    {
        "name": "list_commands",
        "description": (
            "Every command the editor offers RIGHT NOW, as group/label pairs. This is the editor's own "
            "command palette: panels, open documents, entities in the scene, the menu bar, openable "
            "assets, preview viewpoints, actions. Ask this before run_command — the list changes as "
            "documents open and close."
        ),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "run_command",
        "description": (
            "Run one command palette entry, addressed by its group and label exactly as list_commands "
            "reports them. The match is exact on both halves; a near miss is refused with suggestions "
            "rather than run. Returns once a rendered frame already reflects the command."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "group": {"type": "string", "description": "e.g. Panel, Document, Entity, Menu, Open, Preview, Action"},
                "label": {"type": "string", "description": "the label exactly as list_commands reports it"},
            },
            "required": ["group", "label"],
        },
    },
    {
        "name": "get_state",
        "description": (
            "The editor's state as JSON. Sections: scene, selection, documents (open in most-recently-"
            "used order, plus recently closed), panels, renderer_slots (live/pending of six), log "
            "(counts and tail), quiescence. Omit sections for all of them."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "sections": {"type": "array", "items": {"type": "string"}},
            },
        },
    },
    {
        "name": "capture_window",
        "description": (
            "Capture the WHOLE editor to a PNG — the scene AND the interface drawn over it: panels, "
            "menus, dialogs, the document tabs. Taken on a frame that already reflects every command "
            "before it. Use this to prove anything about the interface."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string", "description": "where to write the PNG"}},
            "required": ["path"],
        },
    },
    {
        "name": "capture_viewport",
        "description": (
            "Capture the 3D viewport ONLY, with no interface in it at all. Use this for rendering "
            "evidence. It is NOT a substitute for capture_window and does not contain any panel."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string", "description": "where to write the PNG"}},
            "required": ["path"],
        },
    },
]


def dispatch(channel: Channel, name: str, arguments: dict) -> dict:
    if name == "list_commands":
        return channel.call("commands", wait=120)
    if name == "run_command":
        group = arguments.get("group", "")
        label = arguments.get("label", "")
        if not group or not label:
            raise ValueError("run_command needs both a group and a label; ask list_commands for the pairs.")
        return channel.call("run", group, label)
    if name == "get_state":
        return channel.call("state", *arguments.get("sections", []), wait=120)
    if name == "capture_window":
        path = arguments.get("path", "")
        if not path:
            raise ValueError("capture_window needs a path; a capture with nowhere to go leaves no evidence.")
        return channel.call("shot-window", path)
    if name == "capture_viewport":
        path = arguments.get("path", "")
        if not path:
            raise ValueError("capture_viewport needs a path.")
        return channel.call("shot-viewport", path)
    raise ValueError(f"'{name}' is not a tool this server offers.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socket", required=True, help="the editor's --control-socket path")
    parser.add_argument(
        "--desertctl",
        default=os.environ.get("DESERTCTL", "build/Bin/Debug/DesertCtl"),
        help="path to the DesertCtl binary",
    )
    options = parser.parse_args()
    channel = Channel(options.desertctl, options.socket)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue

        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue  # not a message; a parse error with no id has nowhere to be reported to

        method = message.get("method")
        request_id = message.get("id")

        def reply(result=None, error=None):
            # A notification (no id) gets no answer — that is the JSON-RPC rule, and answering one would
            # put an unexpected message in front of the next real reply.
            if request_id is None:
                return
            body = {"jsonrpc": "2.0", "id": request_id}
            if error is not None:
                body["error"] = error
            else:
                body["result"] = result
            sys.stdout.write(json.dumps(body) + "\n")
            sys.stdout.flush()

        if method == "initialize":
            reply(
                {
                    "protocolVersion": PROTOCOL_VERSION,
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": "desert-editor", "version": "1"},
                }
            )
        elif method == "tools/list":
            reply({"tools": TOOLS})
        elif method == "tools/call":
            params = message.get("params", {})
            try:
                answer = dispatch(channel, params.get("name", ""), params.get("arguments", {}) or {})
                reply({"content": [{"type": "text", "text": json.dumps(answer)}]})
            except (ChannelError, ValueError) as exc:
                # REPORTED AS A FAILED TOOL CALL, not as a protocol error. The editor refusing is a fact
                # about the world that the model needs to read and act on; a JSON-RPC error would be
                # swallowed as a transport fault and the reason would never reach it.
                reply({"content": [{"type": "text", "text": str(exc)}], "isError": True})
        elif method is not None and request_id is not None:
            reply(error={"code": -32601, "message": f"no method '{method}'"})

    return 0


if __name__ == "__main__":
    sys.exit(main())
