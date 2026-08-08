#include <cstdint>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

import Kairo.EngineCore.ShippingRuntime;

TEST_CASE("Phase 13 audio mixer advances loops, retires voices and applies spatial buses")
{
    using namespace kairo::engine;
    RuntimeAudioMixer mixer;
    mixer.AddBus({"Effects",0.5,false});
    mixer.SetListener({0.0,0.0,0.0});
    AudioVoice near; near.Bus="Effects"; near.DurationSeconds=1.0; near.Gain=2.0; near.Spatial=true;
    near.Position={1.0,0.0,0.0}; near.MinimumDistance=1.0; near.MaximumDistance=11.0;
    const auto nearID=mixer.Play(near);
    AudioVoice far=near; far.ID=0u; far.Position={11.0,0.0,0.0}; far.Loop=true;
    const auto farID=mixer.Play(far);
    REQUIRE(nearID!=farID);
    const auto first=mixer.Step(0.25);
    CHECK(first.ActiveVoices==2u);
    CHECK(first.BusLevels.at("Effects")==1.0);
    const auto completed=mixer.Step(0.75);
    CHECK(completed.ActiveVoices==1u);
    CHECK(mixer.Voices().contains(farID));
    CHECK_THROWS_AS(mixer.Stop(nearID),std::out_of_range);
}

TEST_CASE("Phase 14 UI layout, localization, focus and accessibility share one hierarchy")
{
    using namespace kairo::engine;
    LocalizationCatalog catalog;
    catalog.Set("en","play","Play"); catalog.Set("hi","play","Khelen");
    catalog.Set("en","play-label","Start game"); catalog.Set("hi","play-label","Game shuru karen");
    RuntimeUIScene ui(std::move(catalog));
    RuntimeWidget root; root.ID="root"; root.Anchors={0.1,0.1,0.8,0.8}; ui.Add(std::move(root));
    ui.Add({"play","root",RuntimeWidgetKind::Button,{0.25,0.5,0.5,0.25},"play","play-label","game.play",true});
    const auto rect=ui.Layout("play",1000.0,500.0);
    CHECK(rect.X==300.0); CHECK(rect.Y==250.0); CHECK(rect.Width==400.0); CHECK(rect.Height==100.0);
    CHECK(ui.Text("play","hi")=="Khelen");
    CHECK(ui.AccessibleLabel("play","en")=="Start game");
    CHECK(ui.FocusOrder()==std::vector<std::string>{"play"});
    CHECK(ui.Activate("play")=="game.play");
}

TEST_CASE("Phase 15 state deltas and replay hashes detect baseline and simulation divergence")
{
    using namespace kairo::engine;
    RuntimeStateSnapshot baseline{10u,{{"alive",true},{"score",std::int64_t{4}},{"name",std::string{"A"}}}};
    RuntimeStateSnapshot target{11u,{{"alive",true},{"score",std::int64_t{5}},{"speed",2.5}}};
    const auto delta=DiffRuntimeState(baseline,target);
    CHECK(ApplyRuntimeStateDelta(baseline,delta)==target);
    auto wrong=baseline; wrong.Values["score"]=std::int64_t{9};
    CHECK_THROWS_AS(ApplyRuntimeStateDelta(wrong,delta),std::invalid_argument);
    DeterministicReplay replay;
    replay.Record({11u,{"jump"},HashRuntimeState(target)});
    replay.Verify(0u,target);
    target.Values["score"]=std::int64_t{6};
    CHECK_THROWS_AS(replay.Verify(0u,target),std::runtime_error);
    CHECK(SerializeRuntimeState(baseline).starts_with("kairo-state 1\ntick 10\n"));
    CHECK(ParseRuntimeState(SerializeRuntimeState(target))==target);
    const auto path=std::filesystem::temp_directory_path()/"kairo-shipping-runtime-state.ksave";
    SaveRuntimeState(path,target);
    CHECK(LoadRuntimeState(path)==target);
    std::filesystem::remove(path);
}
