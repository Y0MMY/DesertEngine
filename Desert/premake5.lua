deps = include('Dependencies.lua')

include "Desert"
include "Common"

if _OPTIONS["with-tests"] or os.getenv("CI") then
    group ""
    group "Tests"
    include "Tests/"
    group ""
end

newoption {
    trigger = "with-tests",
    description = "Enable test projects generation"
}