\
    #include <catch2/catch_test_macros.hpp>
    #include <cstddef>
    #include <span>
    #include <string_view>
    #include <vector>

    import Kairo.EngineCore.Entity;
    import Kairo.EngineCore.LogicBytecode;
    import Kairo.Foundation.Math.Vector;

    namespace
    {
        class Host final : public kairo::engine::LogicHost
        {
        public:
            kairo::foundation::math::Vec3d Position{};
            void Print(kairo::engine::Entity, std::string_view) override {}
            void SetEntityPosition(kairo::engine::Entity,
                const kairo::foundation::math::Vec3d& position) override { Position = position; }
            void ApplyEntityImpulse(kairo::engine::Entity,
                const kairo::foundation::math::Vec3d&) override {}
        };
    }

    TEST_CASE("Logic bytecode deserializes stateful fields in wire order")
    {
        using namespace kairo::engine;
        LogicProgram program;
        program.RegisterCount = 5;
        program.Vectors.push_back({ 1.25, -2.5, 3.75 });
        program.Entities.push_back(Entity{ 42 });
        program.Instructions = {
            { LogicOpcode::LoadEntity, 3, 0, 0, 0 },
            { LogicOpcode::LoadVector3, 4, 0, 0, 0 },
            { LogicOpcode::SetEntityPosition, 3, 4, 0, 0 },
            { LogicOpcode::Halt, 0, 0, 0, 0 }
        };
        program.Entries.push_back({ LogicEventKind::BeginPlay, {}, 0 });

        const std::vector<std::byte> bytes = SerializeLogicProgram(program);
        const LogicProgram parsed = ParseLogicProgram(bytes);
        REQUIRE(parsed.Vectors.size() == 1);
        CHECK(parsed.Vectors[0].x == 1.25);
        CHECK(parsed.Vectors[0].y == -2.5);
        CHECK(parsed.Vectors[0].z == 3.75);
        REQUIRE(parsed.Instructions.size() == 4);
        CHECK(parsed.Instructions[0].Opcode == LogicOpcode::LoadEntity);
        CHECK(parsed.Instructions[0].A == 3);

        Host host;
        LogicInstance instance(parsed);
        CHECK(instance.Dispatch(Entity{ 7 }, {}, host) == 4);
        CHECK(host.Position.x == 1.25);
        CHECK(host.Position.y == -2.5);
        CHECK(host.Position.z == 3.75);
    }

    TEST_CASE("Logic bytecode rejects every truncated suffix")
    {
        using namespace kairo::engine;
        LogicProgram program;
        program.RegisterCount = 3;
        program.Instructions.push_back({ LogicOpcode::Halt, 0, 0, 0, 0 });
        program.Entries.push_back({ LogicEventKind::BeginPlay, {}, 0 });
        const std::vector<std::byte> bytes = SerializeLogicProgram(program);
        for (std::size_t size = 0; size < bytes.size(); ++size)
            CHECK_THROWS(ParseLogicProgram(std::span(bytes.data(), size)));
    }
