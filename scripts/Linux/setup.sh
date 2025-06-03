#!/bin/sh

# Function to safely install system packages
install_packages() {
    echo "Installing required build tools..."
    sudo apt-get update
    sudo apt-get install -y gawk bison gcc make wget tar clang g++-12 libstdc++-12-dev bear || {
        echo "Failed to install build tools"
        exit 1
    }
    
    echo "Installing additional development libraries..."
    sudo apt-get install -y libxcursor-dev libxinerama-dev libxrandr-dev libxi-dev libgl-dev libxxf86vm-dev || {
        echo "Failed to install development libraries"
        exit 1
    }
}

# Function to install Vulkan components
install_vulkan() {
    echo "Checking Vulkan installation..."
    if ! dpkg -l | grep -q vulkan-tools; then
        echo "Installing Vulkan components..."
        sudo apt-get install -y vulkan-tools libvulkan-dev || {
            echo "Failed to install Vulkan components"
            exit 1
        }
    else
        echo "Vulkan components already installed"
    fi
}

# Function to install Shaderc from prebuilt binaries
install_shaderc() {
    echo "Installing Shaderc..."
    local url="https://storage.googleapis.com/shaderc/artifacts/prod/graphics_shader_compiler/shaderc/linux/continuous_clang_release/495/20250423-103704/install.tgz"
    local temp_dir=$(mktemp -d)
    
    echo "Downloading Shaderc..."
    if command -v wget >/dev/null 2>&1; then
        wget -O "$temp_dir/shaderc.tgz" "$url"
    elif command -v curl >/dev/null 2>&1; then
        curl -L -o "$temp_dir/shaderc.tgz" "$url"
    else
        echo "Error: Need wget or curl to download Shaderc"
        exit 1
    fi

    echo "Extracting Shaderc..."
    tar -xzf "$temp_dir/shaderc.tgz" -C "$temp_dir"

    echo "Installing Shaderc files..."
    # Binaries
    sudo cp -r "$temp_dir/install/bin/"* /usr/local/bin/
    # Headers
    sudo mkdir -p /usr/local/include
    sudo cp -r "$temp_dir/install/include/"* /usr/local/include/
    # Libraries
    sudo cp -r "$temp_dir/install/lib/"* /usr/local/lib/

    # Update linker cache
    sudo ldconfig

    # Cleanup
    rm -rf "$temp_dir"

    echo "Shaderc installed successfully"
}

# Function to install compatible premake version
install_premake() {
    if command -v premake5 >/dev/null 2>&1; then
        premake5 --version
        return 0 
    fi

    local url="https://github.com/premake/premake-core/releases/download/v5.0.0-beta3/premake-5.0.0-beta3-linux.tar.gz"
    local output_file=$(mktemp)
    local temp_dir=$(mktemp -d)

    echo "Downloading premake..."
    if command -v wget >/dev/null 2>&1; then
        wget -O "$output_file" "$url"
    elif command -v curl >/dev/null 2>&1; then
        curl -L -o "$output_file" "$url"
    else
        echo "Error: Need wget or curl to download premake"
        exit 1
    fi

    echo "Installing premake..."
    tar -xzf "$output_file" -C "$temp_dir"
    sudo mv "$temp_dir/premake5" /usr/local/bin/
    sudo chmod +x /usr/local/bin/premake5

    # Cleanup
    rm -f "$output_file"
    rm -rf "$temp_dir"

    # Verify
    if premake5 --version >/dev/null 2>&1; then
        echo "Premake successfully installed:"
        premake5 --version
    else
        echo "Premake installation error"
    fi
}

# Main execution
echo "Starting system setup..."

# Install build tools
install_packages

# Install Vulkan
install_vulkan

# Install Shaderc
install_shaderc

# Install premake
install_premake

echo "Setup completed!"