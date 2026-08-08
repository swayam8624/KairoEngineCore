#include <filesystem>
#include <fstream>
#include <string>
#include <catch2/catch_test_macros.hpp>
import Kairo.EngineCore.Platform;
TEST_CASE("Platform reports a supported host family")
{
    using namespace kairo::engine::platform;
    CHECK(CurrentFamily()!=Family::Unknown);
    CHECK((NativePathSeparator()=='/'||NativePathSeparator()=='\\'));
}
TEST_CASE("Platform file replacement has one cross-platform contract")
{
    using namespace kairo::engine::platform;
    const auto root=std::filesystem::temp_directory_path()/"kairo-platform-tests";
    std::filesystem::create_directories(root);
    const auto source=root/"source.tmp"; const auto destination=root/"destination.txt";
    {std::ofstream out(source,std::ios::binary);out<<"new";}
    {std::ofstream out(destination,std::ios::binary);out<<"old";}
    ReplaceFile(source,destination);
    std::ifstream in(destination,std::ios::binary); std::string value; in>>value;
    CHECK(value=="new"); CHECK_FALSE(std::filesystem::exists(source));
    std::filesystem::remove_all(root);
}
