#include <Common/Core/Serialization/Serialization.hpp>

namespace Common::Serialization
{

    void Node::AddValue( const std::string& key, const std::any& value )
    {
        if ( value.type() == typeid( int ) )
        {
            m_YamlNode[key] = std::any_cast<int>( value );
        }
        else if ( value.type() == typeid( float ) )
        {
            m_YamlNode[key] = std::any_cast<float>( value );
        }
        else if ( value.type() == typeid( double ) )
        {
            m_YamlNode[key] = std::any_cast<double>( value );
        }
        else if ( value.type() == typeid( std::string ) )
        {
            m_YamlNode[key] = std::any_cast<std::string>( value );
        }
        else if ( value.type() == typeid( std::vector<int> ) )
        {
            m_YamlNode[key] = YAML::Node( YAML::NodeType::Sequence );
            for ( const auto& item : std::any_cast<std::vector<int>>( value ) )
            {
                m_YamlNode[key].push_back( item );
            }
        }
        else
        {
            LOG_ERROR( "Unsupported value type {}", key );
        }
    }

    void Node::AddArray( const std::string& key, const std::vector<Node>& array )
    {
        YAML::Node yamlArray = YAML::Node( YAML::NodeType::Sequence );
        for ( const auto& node : array )
        {
            yamlArray.push_back( node.m_YamlNode );
        }
        m_YamlNode[key] = yamlArray;
    }
} // namespace Common::Serialization

namespace Common::Deserialization
{

    NodeReader::NodeReader( const serialized_str& context )
    {
        YAML::Node fileNode = YAML::Load( context );
        MakeContext( fileNode );
    }

    bool NodeReader::LoadFromFile( const std::string& filename )
    {
        YAML::Node fileNode = YAML::LoadFile( filename );
        DESERT_VERIFY( fileNode );

        MakeContext( fileNode );
        return true;
    }

    std::optional<std::any> NodeReader::GetValue( const std::string& key ) const
    {
        auto it = m_Values.find( key );
        if ( it != m_Values.end() )
        {
            return it->second;
        }
        return std::nullopt;
    }

    std::optional<std::vector<NodeReader>> NodeReader::GetChildNodes( const std::string& key ) const
    {
        auto it = m_ChildNodes.find( key );
        if ( it != m_ChildNodes.end() )
        {
            return it->second;
        }
        return {};
    }

    bool NodeReader::HasValue( const std::string& key ) const
    {
        return m_Values.find( key ) != m_Values.end();
    }

    bool NodeReader::HasChildNodes( const std::string& key ) const
    {
        return m_ChildNodes.find( key ) != m_ChildNodes.end();
    }

    void NodeReader::ParseNode( const YAML::Node& node )
    {
        DESERT_VERIFY( node.IsDefined() );

        for ( const auto& it : node )
        {
            std::string key = it.first.as<std::string>();

            if ( it.second.IsScalar() )
            {
                m_Values[key] = it.second.as<std::string>();
            }
            else if ( it.second.IsSequence() )
            {
                std::vector<NodeReader> childNodes;
                for ( const auto& child : it.second )
                {
                    NodeReader childNode;
                    childNode.ParseNode( child );
                    childNodes.push_back( childNode );
                }
                m_ChildNodes[key] = childNodes;
            }
            else if ( it.second.IsMap() )
            {
                NodeReader childNode;
                childNode.ParseNode( it.second );
                m_ChildNodes[key].push_back( childNode );
            }
            else
            {
                DESERT_VERIFY( false );
            }
        }
    }

    void NodeReader::MakeContext( const YAML::Node& node )
    {
        m_YamlNode = node;
        ParseNode( m_YamlNode );
    }

} // namespace Common::Deserialization