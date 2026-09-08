// THE POINTER-OWNERSHIP CENSUS.
//
// The owner asked for an audit of where `shared_ptr`, `unique_ptr` and raw pointers are used and which
// of them each place should be. The failure mode of that request is five hundred lines of taste, so
// every verdict here is the answer to two FACTUAL questions instead:
//
//   Q1. Who is OBLIGED to destroy this object?  One known owner -> unique_ptr. Several owners whose
//       deaths are not ordered -> shared_ptr. No owner, only a watcher -> a raw pointer or a weak_ptr,
//       and then Q2 is compulsory.
//   Q2. Can the observed die before the observer?  Yes and unguarded -> that is a DEFECT, not a style.
//       No -> the row must name WHAT guarantees it.
//
// The subject is the pointer-typed DATA MEMBER (pointer_ownership_scan.hpp says why), the verdicts are
// in pointer_ownership_register.hpp, and this file is what makes them fail.
//
// WHY A CENSUS AND NOT A DOCUMENT IN `Docs/`. A report goes stale in silence; this goes red. A pointer
// member added tomorrow has no row, and `EveryRawPointerMemberNamesItsGuard` names it with its file and
// line and asks its author the two questions above.

#include "pointer_ownership_register.hpp"
#include "pointer_ownership_scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace Desert::Tests::PointerCensus;
    namespace Text = Desert::Tests::ConsumerText;

    const std::vector<Member>& Members()
    {
        static const std::vector<Member> members = []
        {
            const std::string root = RepoRoot();
            return root.empty() ? std::vector<Member>{} : ScanMembers( root );
        }();
        return members;
    }

    int CountOf( Form f )
    {
        int n = 0;
        for ( const Member& m : Members() )
            n += m.Kind == f ? 1 : 0;
        return n;
    }

    const Row* FindRow( const Member& m )
    {
        for ( const Row& r : Register() )
            if ( m.Class == r.Class && m.Name == r.Member && m.File == r.File )
                return &r;
        return nullptr;
    }

    std::string ReadRepoFile( const char* relative )
    {
        return Text::StripCommentsAndLiterals( ReadAll( fs::path( RepoRoot() ) / relative ) );
    }
} // namespace

// ------------------------------------------------------------------------------------------------
// The number
// ------------------------------------------------------------------------------------------------

TEST( PointerOwnership, TheScanFindsTheCensusedPopulation )
{
    ASSERT_FALSE( RepoRoot().empty() ) << "could not locate the repository from the working directory";

    // A census that finds nothing to census has stopped working, and it fails in the confident voice it
    // uses when a tree really is clean. This is the line that tells the two apart.
    ASSERT_FALSE( Members().empty() ) << "the scan found no pointer members at all -- the reader is blind, "
                                         "and every assertion below is vacuous.";

    // MEASURED ON dev @ 8f7f49b4, not estimated. Whole tree, the same scan over a wider ScannedTrees():
    // 738 members (316 raw / 282 shared / 106 unique / 34 weak). Stage 1 covers Graphic, ShaderResources
    // and Assets, where the cost of a lifetime mistake is a use-after-free of a device object.
    EXPECT_EQ( CountOf( Form::Raw ), 145 );
    EXPECT_EQ( CountOf( Form::Shared ), 196 );
    EXPECT_EQ( CountOf( Form::Unique ), 37 );
    EXPECT_EQ( CountOf( Form::Weak ), 17 );
    EXPECT_EQ( (int)Members().size(), 395 )
         << "the population moved. That is not a number to adjust -- it means a pointer member was added "
            "or removed, and the two questions at the top of this file are owed an answer for it.";
}

// ------------------------------------------------------------------------------------------------
// Every raw pointer answers Q2
// ------------------------------------------------------------------------------------------------

TEST( PointerOwnership, EveryRawPointerMemberNamesItsGuard )
{
    ASSERT_FALSE( Members().empty() );

    int unlisted = 0;
    for ( const Member& m : Members() )
    {
        if ( m.Kind != Form::Raw )
            continue;
        if ( FindRow( m ) != nullptr )
            continue;
        ++unlisted;
        ADD_FAILURE()
             << m.File << ":" << m.Line << " " << m.Class << "::" << m.Name << " (" << m.Decl
             << ")\nis a raw pointer member with no row in the register. A raw pointer answers NEITHER "
                "ownership question by itself, so add a row to pointer_ownership_register.hpp saying:\n"
                "  (1) who is obliged to destroy the pointee, and\n"
                "  (2) what stops it dying before this object does.\n"
                "If the answer to (2) is 'nothing', that is a defect: fix it, or file it as Guard::Debt "
                "with the task that owns the fix.";
    }
    EXPECT_EQ( unlisted, 0 );
}

TEST( PointerOwnership, TheRegisterDescribesMembersThatStillExist )
{
    // The other direction, and the one a census normally loses first: a row whose member was renamed or
    // deleted goes on asserting nothing while looking like coverage. DeviceLostCensus lost sight this
    // way, which is why the check is here rather than assumed.
    ASSERT_FALSE( Members().empty() );

    for ( const Row& r : Register() )
    {
        const bool alive = std::any_of( Members().begin(), Members().end(),
                                        [&r]( const Member& m ) {
                                            return m.Kind == Form::Raw && m.File == r.File &&
                                                   m.Class == r.Class && m.Name == r.Member;
                                        } );
        EXPECT_TRUE( alive ) << r.File << " " << r.Class << "::" << r.Member
                             << " has a register row but the scan no longer finds the member. Delete the "
                                "row, or find out why the scan stopped seeing it.";
    }
}

TEST( PointerOwnership, EveryRowCarriesAnArgument )
{
    for ( const Row& r : Register() )
    {
        EXPECT_GT( std::string( r.Why ).size(), 30u )
             << r.Class << "::" << r.Member
             << " has no argument. A row without one is a name in a list, which is what this census "
                "exists instead of.";
        if ( r.How != Guard::Debt )
            continue;

        // A DEBT WITH NO TASK NAME IS UNREADABLE IN A MONTH. The same rule ConfigOwnership enforces on
        // its own debt register, for the same reason.
        EXPECT_FALSE( std::string( r.Task ).empty() )
             << r.Class << "::" << r.Member
             << " is recorded as a live lifetime defect and names no task. Name the task that owns the "
                "fix, or fix it here.";
    }
}

// ------------------------------------------------------------------------------------------------
// The guards that rest on a checkable fact are checked
// ------------------------------------------------------------------------------------------------

TEST( PointerOwnership, MaterialPropertyStorageIsAddressStable )
{
    // 46 of the 145 rows rest on ONE argument: a material's cached `Texture2DProperty*` cannot dangle
    // because the property lives in the material's own executor. That argument has three legs and all
    // three are facts about the source, so all three are checked here rather than believed.
    ASSERT_FALSE( RepoRoot().empty() );

    const std::string executorHpp =
         ReadRepoFile( "Desert/Desert/Source/Engine/Graphic/Materials/MaterialExecutor.hpp" );
    const std::string executorCpp =
         ReadRepoFile( "Desert/Desert/Source/Engine/Graphic/Materials/MaterialExecutor.cpp" );
    const std::string materialHpp =
         ReadRepoFile( "Desert/Desert/Source/Engine/Graphic/Materials/Material.hpp" );

    // (1) The storage holds HANDLES, not objects. `std::vector<T>` would move every property when it
    //     grew, and all 46 cached pointers would dangle on the next `emplace`.
    EXPECT_NE( executorHpp.find( "using PropertyStorage = std::vector<std::shared_ptr<T>>" ),
               std::string::npos )
         << "MaterialExecutor::PropertyStorage is no longer a vector of shared_ptr. If the properties "
            "are stored BY VALUE, every material's cached property pointer dangles the moment the "
            "vector grows.";

    // (2) The material owns the executor, so the observed dies with the observer.
    EXPECT_NE( materialHpp.find( "std::unique_ptr<MaterialExecutor> m_MaterialExecutor" ),
               std::string::npos )
         << "Material no longer owns its MaterialExecutor by unique_ptr; the 46 cached property pointers "
            "lose the reason they cannot outlive their pointee.";

    // (3) The table is built ONCE, in the constructor, and never rebuilt. A shader hot-reload that
    //     called InitializeProperties() again would leave every cached pointer pointing at a released
    //     property.
    int initializeCalls = 0;
    for ( std::size_t at : Text::WordPositions( executorCpp, "InitializeProperties" ) )
    {
        // The definition itself (`void MaterialExecutor::InitializeProperties()`) is not a call.
        const std::string before = executorCpp.substr( at < 64 ? 0 : at - 64, at < 64 ? at : 64 );
        if ( before.find( "MaterialExecutor::" ) != std::string::npos )
            continue;
        ++initializeCalls;
    }
    EXPECT_EQ( initializeCalls, 1 ) << "InitializeProperties() is called " << initializeCalls
                                    << " times. It must be called exactly once, from the constructor: a "
                                       "second call rebuilds the property table under 46 cached pointers.";

    for ( const char* mutation : { "PropertiesStorage.clear", "PropertiesStorage.erase",
                                   "PropertiesStorage.resize", "PropertiesStorage.pop_back" } )
    {
        EXPECT_EQ( executorCpp.find( mutation ), std::string::npos )
             << "MaterialExecutor now does `" << mutation
             << "`. Removing a property releases the object 46 materials hold a raw pointer to.";
        EXPECT_EQ( executorHpp.find( mutation ), std::string::npos );
    }
}

TEST( PointerOwnership, TheUniformImageKeepsTheDescriptorAndNotTheImage )
{
    // The shape this audit wants preferred, asserted where it already exists: a class that must survive
    // an image it does not own keeps a COPY OF THE VALUE it needs (the descriptor, snapshotted at set
    // time) rather than a handle on somebody else's lifetime. Г12 removed the pointers; this is what
    // stops them coming back.
    ASSERT_FALSE( RepoRoot().empty() );

    for ( const char* header :
          { "Desert/Desert/Source/Engine/ShaderResources/API/Vulkan/VulkanUniformImage2D.hpp",
            "Desert/Desert/Source/Engine/ShaderResources/API/Vulkan/VulkanUniformImageCube.hpp" } )
    {
        const std::string src = ReadRepoFile( header );
        EXPECT_NE( src.find( "VkDescriptorImageInfo m_DescriptorInfo" ), std::string::npos )
             << header << " must keep the descriptor it copied out at SetImage time.";
        EXPECT_EQ( src.find( "Graphic::Image2D* m_Image" ), std::string::npos )
             << header << " has taken a raw pointer to an image it does not own back as a member.";
        EXPECT_EQ( src.find( "Graphic::ImageCube* m_Image" ), std::string::npos ) << header;
    }
}

TEST( PointerOwnership, NoRawPointerMemberIsDeletedByItsHolder )
{
    // AN OWNING RAW POINTER WEARING AN OBSERVER'S CLOTHES. `delete m_Something` in a class whose
    // `m_Something` is a raw pointer member means Q1's answer is "this class" and the type does not say
    // so — which is the one case where the audit's verdict is mechanical: it should be a unique_ptr.
    // The two legitimate owning raws in this tree hold VMA handles, which have no C++ destructor and
    // are released through the allocator; neither is `delete`d.
    ASSERT_FALSE( Members().empty() );

    // THE MEMBER IS DECLARED IN THE HEADER AND DELETED IN THE .cpp, so scanning only the declaring file
    // sees nothing. Caught by the mutation check for this very test: a `delete m_Backdrop;` planted in
    // Render2D.cpp left it green. The class's own translation unit is where a class frees its own
    // members, so both halves of the pair are read.
    std::map<std::string, std::vector<std::string>> byFile;
    for ( const Member& m : Members() )
    {
        if ( m.Kind != Form::Raw )
            continue;
        byFile[m.File].push_back( m.Name );
        if ( m.File.size() > 4 && m.File.compare( m.File.size() - 4, 4, ".hpp" ) == 0 )
        {
            const std::string sibling = m.File.substr( 0, m.File.size() - 4 ) + ".cpp";
            if ( fs::exists( fs::path( RepoRoot() ) / sibling ) )
                byFile[sibling].push_back( m.Name );
        }
    }

    for ( const auto& [file, names] : byFile )
    {
        const std::string src = ReadRepoFile( file.c_str() );
        for ( std::size_t at : Text::WordPositions( src, "delete" ) )
        {
            const std::size_t i    = Text::SkipSpace( src, at + 6 );
            const std::string what = Text::IdentAt( src, i );
            for ( const std::string& name : names )
            {
                if ( what != name )
                    continue;
                ADD_FAILURE() << file << ":" << LineOf( src, at ) << " deletes its own raw pointer member `"
                              << name
                              << "`. That is sole ownership spelled without saying so -- make it a "
                                 "std::unique_ptr, which cannot be forgotten on an early return.";
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// The shared_ptr half: a claim of shared ownership, and what it costs when it is not true
// ------------------------------------------------------------------------------------------------

TEST( PointerOwnership, SharedOwnershipIsTheMajorityAndThatIsTheMeasuredAnswer )
{
    // THE AUDIT'S LARGEST SINGLE RESULT IS A REFUSAL, and it is recorded here so the next person does
    // not re-derive it. 196 of the 395 members in these trees are `shared_ptr`, and for the GPU
    // resources that is the CORRECT form rather than a habit: an Image2D is held at once by the
    // framebuffer that allocated it, by the descriptor sets that sample it and by the deletion queue
    // that outlives both, and no two of those have an ordered death. Converting them to `unique_ptr`
    // would not be a cleanup, it would be a use-after-free.
    //
    // What the audit did NOT find is worth saying plainly: not one `shared_ptr` member in these trees
    // sits in a per-frame hot loop where the atomic refcount pair is measurable. The cost of a wrong
    // `shared_ptr` here is a false impression of shared ownership, and the register's job is to make
    // the true owner findable instead of mass-replacing them for uniformity -- churn that would hide
    // the seven real findings in a diff of two hundred files.
    EXPECT_EQ( CountOf( Form::Shared ), 196 );
    EXPECT_GT( CountOf( Form::Shared ), CountOf( Form::Unique ) + CountOf( Form::Weak ) );
}

int main( int argc, char** argv )
{
    ::testing::InitGoogleTest( &argc, argv );
    return RUN_ALL_TESTS();
}
