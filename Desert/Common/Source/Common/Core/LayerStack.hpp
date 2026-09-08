#pragma once

#include <memory>
#include <vector>

#include "Layer.hpp"

namespace Common
{
    // OWNS THE LAYERS IT IS GIVEN, and that sentence is the whole change A8-2 made here.
    //
    // It used to hold `std::vector<Layer*>`, `PushLayer` took a raw pointer, `~LayerStack` was empty and
    // nothing anywhere deleted a layer — so the answer to "who is obliged to destroy this?" was NOBODY.
    // Both call sites in the tree pass `new EditorLayer(...)` / `new RuntimeLayer(...)`, so the editor's
    // whole layer, and every resource whose release lives in a layer destructor, was leaked at exit with
    // no diagnostic. It survived because `~EditorLayer` is `= default` and the real teardown is done
    // explicitly in `OnDetach`; the trap was that anybody adding release work to a layer destructor would
    // have found it silently never running.
    //
    // One owner, known, obliged to destroy: that is a `unique_ptr`, and taking the layer BY VALUE at the
    // door is what makes the obligation impossible to miscount at a call site.
    //
    // `PopLayer` is gone with it. It removed the layer from the vector and deleted nothing, it was called
    // from nowhere in the tree (nor was `Application::PopLayer`), and under ownership it would have become
    // a silent destroy — a dead function that had turned into a trap.
    class LayerStack
    {
    public:
        LayerStack()  = default;
        ~LayerStack() = default;

        LayerStack( const LayerStack& )            = delete;
        LayerStack& operator=( const LayerStack& ) = delete;

        /// Takes ownership. Returns a borrowed pointer to the layer now inside the stack, so the caller
        /// can attach it without holding a second claim on its lifetime.
        Layer* PushLayer( std::unique_ptr<Layer> layer );

        std::vector<std::unique_ptr<Layer>>::iterator begin()
        {
            return m_Layers.begin();
        }
        std::vector<std::unique_ptr<Layer>>::iterator end()
        {
            return m_Layers.end();
        }

    private:
        std::vector<std::unique_ptr<Layer>> m_Layers;
    };
} // namespace Common
