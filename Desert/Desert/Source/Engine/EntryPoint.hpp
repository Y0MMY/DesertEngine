#pragma once

extern Desert::Engine::Applicaton* CreateApplication( int argc, char** argv );

int main(int argc, char** argv)
{
    Common::Logger::LogInit();

	auto app = CreateApplication(argc, argv);

	return 0;
}