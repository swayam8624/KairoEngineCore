module;

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.EngineCore.ShippingRuntime;

import Kairo.EngineCore.TextFormat;

export namespace kairo::engine
{
    struct RuntimeVec3 final { double X = 0.0, Y = 0.0, Z = 0.0; };

    struct AudioBus final
    {
        std::string Name;
        double Gain = 1.0;
        bool Muted = false;
    };

    struct AudioVoice final
    {
        std::uint64_t ID = 0u;
        std::string Bus = "Master";
        double DurationSeconds = 0.0;
        double CursorSeconds = 0.0;
        double Gain = 1.0;
        bool Loop = false;
        bool Spatial = false;
        RuntimeVec3 Position{};
        double MinimumDistance = 1.0;
        double MaximumDistance = 100.0;
    };

    struct AudioMixFrame final
    {
        std::map<std::string, double> BusLevels;
        std::size_t ActiveVoices = 0u;
    };

    /// Task: own deterministic voice, bus, and spatial attenuation state while
    /// platform audio adapters remain responsible only for device I/O.
    class RuntimeAudioMixer final
    {
    public:
        RuntimeAudioMixer() { AddBus({ "Master", 1.0, false }); }

        /// Input: a unique bus with finite non-negative gain.
        /// Output: the bus becomes immediately available to subsequently played voices.
        void AddBus(AudioBus bus)
        {
            ValidateName(bus.Name, "Audio bus");
            if (!std::isfinite(bus.Gain) || bus.Gain < 0.0 || bus.Gain > 16.0)
                throw std::invalid_argument("Audio bus gain must be finite and in [0, 16].");
            if (!m_Buses.emplace(bus.Name, std::move(bus)).second)
                throw std::invalid_argument("Audio bus names must be unique.");
        }

        void SetListener(RuntimeVec3 position)
        {
            if (!Finite(position)) throw std::invalid_argument("Audio listener position must be finite.");
            m_Listener = position;
        }

        /// Input: a fully authored voice. ID zero requests the next stable runtime ID.
        /// Output: the stable voice ID used by stop/pause and device adapters.
        [[nodiscard]] std::uint64_t Play(AudioVoice voice)
        {
            if (!m_Buses.contains(voice.Bus)) throw std::out_of_range("Audio voice references an unknown bus.");
            if (!std::isfinite(voice.DurationSeconds) || voice.DurationSeconds <= 0.0 ||
                !std::isfinite(voice.CursorSeconds) || voice.CursorSeconds < 0.0 ||
                !std::isfinite(voice.Gain) || voice.Gain < 0.0 || voice.Gain > 16.0 || !Finite(voice.Position) ||
                !std::isfinite(voice.MinimumDistance) || !std::isfinite(voice.MaximumDistance) ||
                voice.MinimumDistance <= 0.0 || voice.MaximumDistance < voice.MinimumDistance)
                throw std::invalid_argument("Audio voice parameters are invalid.");
            if (!voice.Loop && voice.CursorSeconds >= voice.DurationSeconds)
                throw std::invalid_argument("A non-looping audio voice must begin before its end.");
            if (m_Voices.size() >= 65536u) throw std::length_error("Audio mixer exceeds 65536 active voices.");
            if (voice.ID == 0u) voice.ID = m_NextVoice++;
            else m_NextVoice = std::max(m_NextVoice, voice.ID + 1u);
            if (!m_Voices.emplace(voice.ID, std::move(voice)).second)
                throw std::invalid_argument("Audio voice IDs must be unique.");
            return voice.ID;
        }

        void Stop(std::uint64_t id)
        {
            if (m_Voices.erase(id) == 0u) throw std::out_of_range("Audio voice was not active.");
        }

        /// Input: a finite non-negative runtime delta.
        /// Output: levels after advancing cursors and retiring completed voices.
        [[nodiscard]] AudioMixFrame Step(double deltaSeconds)
        {
            if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0)
                throw std::invalid_argument("Audio mix delta must be finite and non-negative.");
            AudioMixFrame frame;
            for (auto iterator = m_Voices.begin(); iterator != m_Voices.end();)
            {
                auto& voice = iterator->second;
                voice.CursorSeconds += deltaSeconds;
                if (voice.Loop) voice.CursorSeconds = std::fmod(voice.CursorSeconds, voice.DurationSeconds);
                else if (voice.CursorSeconds >= voice.DurationSeconds)
                {
                    iterator = m_Voices.erase(iterator);
                    continue;
                }
                const auto& bus = m_Buses.at(voice.Bus);
                const auto& master = m_Buses.at("Master");
                double attenuation = 1.0;
                if (voice.Spatial)
                {
                    const double distance = Distance(voice.Position, m_Listener);
                    if (distance >= voice.MaximumDistance) attenuation = 0.0;
                    else if (distance > voice.MinimumDistance)
                        attenuation = 1.0 - (distance - voice.MinimumDistance) /
                            (voice.MaximumDistance - voice.MinimumDistance);
                }
                const double busGain = voice.Bus == "Master" ? master.Gain : bus.Gain * master.Gain;
                const double level = bus.Muted || master.Muted ? 0.0
                    : voice.Gain * busGain * attenuation;
                frame.BusLevels[voice.Bus] += level;
                ++iterator;
            }
            frame.ActiveVoices = m_Voices.size();
            return frame;
        }

        [[nodiscard]] const std::map<std::uint64_t, AudioVoice>& Voices() const noexcept { return m_Voices; }

    private:
        std::map<std::string, AudioBus> m_Buses;
        std::map<std::uint64_t, AudioVoice> m_Voices;
        RuntimeVec3 m_Listener{};
        std::uint64_t m_NextVoice = 1u;

        static void ValidateName(std::string_view value, std::string_view role)
        { if (value.empty() || value.size() > 128u) throw std::invalid_argument(std::string(role) + " name is invalid."); }
        static bool Finite(RuntimeVec3 value) noexcept
        { return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z); }
        static double Distance(RuntimeVec3 a, RuntimeVec3 b) noexcept
        { const double x=a.X-b.X,y=a.Y-b.Y,z=a.Z-b.Z; return std::sqrt(x*x+y*y+z*z); }
    };

    enum class RuntimeWidgetKind : std::uint8_t { Container, Text, Image, Button, Slider };
    struct RuntimeRect final { double X=0.0, Y=0.0, Width=1.0, Height=1.0; };
    struct RuntimeWidget final
    {
        std::string ID;
        std::optional<std::string> Parent;
        RuntimeWidgetKind Kind = RuntimeWidgetKind::Container;
        RuntimeRect Anchors{};
        std::string TextKey;
        std::string AccessibleLabelKey;
        std::string Action;
        bool Focusable = false;
    };
    struct RuntimePixelRect final { double X=0.0, Y=0.0, Width=0.0, Height=0.0; };

    class LocalizationCatalog final
    {
    public:
        void Set(std::string locale, std::string key, std::string value)
        {
            Validate(locale, "Locale"); Validate(key, "Localization key");
            if (value.size() > 16384u) throw std::length_error("Localized value exceeds 16 KiB.");
            m_Values[std::move(locale)][std::move(key)] = std::move(value);
        }
        [[nodiscard]] std::string Resolve(std::string_view locale, std::string_view key,
            std::string_view fallbackLocale = "en") const
        {
            if (const auto language = m_Values.find(std::string(locale)); language != m_Values.end())
                if (const auto value = language->second.find(std::string(key)); value != language->second.end()) return value->second;
            if (const auto fallback = m_Values.find(std::string(fallbackLocale)); fallback != m_Values.end())
                if (const auto value = fallback->second.find(std::string(key)); value != fallback->second.end()) return value->second;
            throw std::out_of_range("Localization key has no requested or fallback translation.");
        }
    private:
        std::map<std::string, std::map<std::string, std::string>> m_Values;
        static void Validate(std::string_view value, std::string_view role)
        { if (value.empty() || value.size()>256u) throw std::invalid_argument(std::string(role)+" is invalid."); }
    };

    /// Task: validate a renderer-neutral UI hierarchy and expose deterministic
    /// layout, focus order, localized text, and accessibility labels.
    class RuntimeUIScene final
    {
    public:
        explicit RuntimeUIScene(LocalizationCatalog catalog = {}) : m_Catalog(std::move(catalog)) {}
        void Add(RuntimeWidget widget)
        {
            if (widget.ID.empty() || widget.ID.size()>128u) throw std::invalid_argument("Runtime UI widget ID is invalid.");
            const auto& a=widget.Anchors;
            if (!Finite(a.X)||!Finite(a.Y)||!Finite(a.Width)||!Finite(a.Height)||a.X<0.0||a.Y<0.0||a.Width<0.0||a.Height<0.0||a.X+a.Width>1.0||a.Y+a.Height>1.0)
                throw std::invalid_argument("Runtime UI anchors must be a normalized rectangle.");
            if (widget.Parent && !m_Widgets.contains(*widget.Parent))
                throw std::out_of_range("Runtime UI parent must be added before its child.");
            if (widget.Focusable && widget.AccessibleLabelKey.empty())
                throw std::invalid_argument("Focusable runtime UI widgets require an accessible label.");
            if (m_Widgets.size() >= 65536u) throw std::length_error("Runtime UI exceeds 65536 widgets.");
            if (!m_Widgets.emplace(widget.ID, std::move(widget)).second)
                throw std::invalid_argument("Runtime UI widget IDs must be unique.");
        }
        [[nodiscard]] RuntimePixelRect Layout(std::string_view id, double width, double height) const
        {
            if (!Finite(width)||!Finite(height)||width<=0.0||height<=0.0) throw std::invalid_argument("Runtime UI viewport is invalid.");
            const auto& widget=m_Widgets.at(std::string(id)); RuntimePixelRect parent{0.0,0.0,width,height};
            if (widget.Parent) parent=Layout(*widget.Parent,width,height);
            return {parent.X+widget.Anchors.X*parent.Width,parent.Y+widget.Anchors.Y*parent.Height,
                widget.Anchors.Width*parent.Width,widget.Anchors.Height*parent.Height};
        }
        [[nodiscard]] std::vector<std::string> FocusOrder() const
        {
            std::vector<std::string> result; for(const auto& [id,w]:m_Widgets) if(w.Focusable) result.push_back(id); return result;
        }
        [[nodiscard]] std::string Text(std::string_view id,std::string_view locale) const
        { return m_Catalog.Resolve(locale,m_Widgets.at(std::string(id)).TextKey); }
        [[nodiscard]] std::string AccessibleLabel(std::string_view id,std::string_view locale) const
        { return m_Catalog.Resolve(locale,m_Widgets.at(std::string(id)).AccessibleLabelKey); }
        [[nodiscard]] std::string Activate(std::string_view id) const
        { const auto& w=m_Widgets.at(std::string(id)); if(!w.Focusable||w.Action.empty()) throw std::logic_error("Runtime UI widget has no action."); return w.Action; }
    private:
        std::map<std::string,RuntimeWidget> m_Widgets;
        LocalizationCatalog m_Catalog;
        static bool Finite(double value) noexcept { return std::isfinite(value); }
    };

    using RuntimeStateValue = std::variant<bool,std::int64_t,double,std::string>;
    [[nodiscard]] inline bool RuntimeStateValuesEqual(const RuntimeStateValue& a,
        const RuntimeStateValue& b)
    {
        if(a.index()!=b.index()) return false;
        switch(a.index())
        {
            case 0u: return std::get<bool>(a)==std::get<bool>(b);
            case 1u: return std::get<std::int64_t>(a)==std::get<std::int64_t>(b);
            case 2u: return std::bit_cast<std::uint64_t>(std::get<double>(a))==
                std::bit_cast<std::uint64_t>(std::get<double>(b));
            case 3u: return std::get<std::string>(a)==std::get<std::string>(b);
            default: return false;
        }
    }
    struct RuntimeStateSnapshot final
    {
        std::uint64_t Tick=0u;
        std::map<std::string,RuntimeStateValue> Values;
        friend bool operator==(const RuntimeStateSnapshot& a,const RuntimeStateSnapshot& b)
        {
            if(a.Tick!=b.Tick||a.Values.size()!=b.Values.size()) return false;
            auto left=a.Values.begin(); auto right=b.Values.begin();
            for(;left!=a.Values.end();++left,++right)
                if(left->first!=right->first||!RuntimeStateValuesEqual(left->second,right->second)) return false;
            return true;
        }
    };
    struct RuntimeStateDelta final
    {
        std::uint64_t BaselineHash=0u;
        std::uint64_t Tick=0u;
        std::map<std::string,std::optional<RuntimeStateValue>> Changes;
    };

    namespace detail
    {
        [[nodiscard]] inline std::string StateHex(std::string_view text)
        {
            static constexpr char digits[]="0123456789abcdef";
            std::string out; out.reserve(text.size()*2u);
            for(unsigned char c:text){out.push_back(digits[c>>4u]);out.push_back(digits[c&15u]);}
            return out;
        }
        [[nodiscard]] inline std::string StateUnhex(std::string_view text)
        {
            if(text.size()%2u!=0u) throw std::invalid_argument("Runtime state hex field has odd length.");
            const auto nibble=[](char c)->unsigned
            {
                if(c>='0'&&c<='9') return static_cast<unsigned>(c-'0');
                if(c>='a'&&c<='f') return static_cast<unsigned>(c-'a'+10);
                throw std::invalid_argument("Runtime state hex field contains an invalid digit.");
            };
            std::string result; result.reserve(text.size()/2u);
            for(std::size_t i=0u;i<text.size();i+=2u)
                result.push_back(static_cast<char>((nibble(text[i])<<4u)|nibble(text[i+1u])));
            return result;
        }
        template<class Integer>
        [[nodiscard]] inline Integer StateInteger(std::string_view text)
        {
            Integer value{}; const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),value);
            if(error!=std::errc{}||end!=text.data()+text.size()) throw std::invalid_argument("Runtime state integer is invalid.");
            return value;
        }
    }

    [[nodiscard]] inline std::string SerializeRuntimeState(const RuntimeStateSnapshot& snapshot)
    {
        if(snapshot.Values.size()>65536u) throw std::length_error("Runtime state exceeds 65536 values.");
        std::string result="kairo-state 1\ntick "+std::to_string(snapshot.Tick)+"\n";
        for(const auto& [key,value]:snapshot.Values)
        {
            if(key.empty()||key.size()>256u) throw std::invalid_argument("Runtime state key is invalid.");
            result += std::visit([&](const auto& item)->std::string
            {
                using T=std::decay_t<decltype(item)>;
                if constexpr(std::is_same_v<T,bool>) return "bool "+detail::StateHex(key)+" "+(item?"1":"0")+"\n";
                else if constexpr(std::is_same_v<T,std::int64_t>) return "int "+detail::StateHex(key)+" "+std::to_string(item)+"\n";
                else if constexpr(std::is_same_v<T,double>)
                { if(!std::isfinite(item)) throw std::invalid_argument("Runtime state real must be finite."); return "real "+detail::StateHex(key)+" "+std::to_string(std::bit_cast<std::uint64_t>(item))+"\n"; }
                else { if(item.size()>1048576u) throw std::length_error("Runtime state text exceeds 1 MiB."); return "text "+detail::StateHex(key)+" "+detail::StateHex(item)+"\n"; }
            },value);
            if(result.size()>16u*1024u*1024u) throw std::length_error("Runtime state exceeds 16 MiB.");
        }
        return result;
    }

    /// Input: bounded canonical `kairo-state 1` text.
    /// Output: the exact typed snapshot or a located-by-statement rejection.
    [[nodiscard]] inline RuntimeStateSnapshot ParseRuntimeState(std::string_view source)
    {
        if(source.size()>16u*1024u*1024u) throw std::length_error("Runtime state exceeds 16 MiB.");
        RuntimeStateSnapshot result; bool header=false,tick=false; std::size_t offset=0u;
        while(offset<=source.size())
        {
            const std::size_t end=source.find('\n',offset);
            const std::string_view line=source.substr(offset,end==std::string_view::npos?source.size()-offset:end-offset);
            offset=end==std::string_view::npos?source.size()+1u:end+1u;
            if(line.empty()) continue;
            if(!header)
            { if(line!="kairo-state 1") throw std::invalid_argument("Runtime state header is missing or unsupported."); header=true; continue; }
            const std::size_t first=line.find(' ');
            if(first==std::string_view::npos) throw std::invalid_argument("Runtime state statement has no value.");
            const auto type=line.substr(0u,first);
            if(type=="tick")
            { if(tick) throw std::invalid_argument("Runtime state has duplicate tick statements."); result.Tick=detail::StateInteger<std::uint64_t>(line.substr(first+1u)); tick=true; continue; }
            if(!tick) throw std::invalid_argument("Runtime state values cannot precede the tick.");
            const std::size_t second=line.find(' ',first+1u);
            if(second==std::string_view::npos) throw std::invalid_argument("Runtime state value statement is incomplete.");
            const std::string key=detail::StateUnhex(line.substr(first+1u,second-first-1u));
            if(key.empty()||key.size()>256u||result.Values.contains(key)) throw std::invalid_argument("Runtime state key is empty, oversized, or duplicated.");
            const auto value=line.substr(second+1u);
            if(type=="bool")
            { if(value!="0"&&value!="1") throw std::invalid_argument("Runtime state boolean is invalid."); result.Values[key]=value=="1"; }
            else if(type=="int") result.Values[key]=detail::StateInteger<std::int64_t>(value);
            else if(type=="real")
            { const double real=std::bit_cast<double>(detail::StateInteger<std::uint64_t>(value)); if(!std::isfinite(real)) throw std::invalid_argument("Runtime state real is non-finite."); result.Values[key]=real; }
            else if(type=="text")
            { auto text=detail::StateUnhex(value); if(text.size()>1048576u) throw std::length_error("Runtime state text exceeds 1 MiB."); result.Values[key]=std::move(text); }
            else throw std::invalid_argument("Runtime state statement type is unknown.");
            if(result.Values.size()>65536u) throw std::length_error("Runtime state exceeds 65536 values.");
        }
        if(!header||!tick) throw std::invalid_argument("Runtime state is incomplete.");
        return result;
    }

    inline void SaveRuntimeState(const std::filesystem::path& path,const RuntimeStateSnapshot& snapshot)
    { SaveTextFileAtomically(path,SerializeRuntimeState(snapshot),"runtime state snapshot"); }
    [[nodiscard]] inline RuntimeStateSnapshot LoadRuntimeState(const std::filesystem::path& path)
    { return ParseRuntimeState(LoadBoundedTextFile(path,16u*1024u*1024u,"runtime state snapshot")); }

    [[nodiscard]] inline std::uint64_t HashRuntimeState(const RuntimeStateSnapshot& snapshot)
    {
        std::uint64_t hash=1469598103934665603ULL;
        for(unsigned char byte:SerializeRuntimeState(snapshot)){hash^=byte;hash*=1099511628211ULL;}
        return hash;
    }

    [[nodiscard]] inline RuntimeStateDelta DiffRuntimeState(const RuntimeStateSnapshot& baseline,
        const RuntimeStateSnapshot& target)
    {
        if(target.Tick<baseline.Tick) throw std::invalid_argument("Runtime state target tick precedes its baseline.");
        RuntimeStateDelta result{HashRuntimeState(baseline),target.Tick,{}};
        for(const auto& [key,value]:target.Values)
        { const auto found=baseline.Values.find(key); if(found==baseline.Values.end()||!RuntimeStateValuesEqual(found->second,value)) result.Changes[key]=value; }
        for(const auto& [key,value]:baseline.Values) if(!target.Values.contains(key)) result.Changes[key]=std::nullopt;
        return result;
    }

    [[nodiscard]] inline RuntimeStateSnapshot ApplyRuntimeStateDelta(RuntimeStateSnapshot baseline,
        const RuntimeStateDelta& delta)
    {
        if(HashRuntimeState(baseline)!=delta.BaselineHash) throw std::invalid_argument("Runtime state delta baseline hash does not match.");
        for(const auto& [key,value]:delta.Changes) { if(value) baseline.Values[key]=*value; else baseline.Values.erase(key); }
        baseline.Tick=delta.Tick; return baseline;
    }

    struct ReplayFrame final { std::uint64_t Tick=0u; std::vector<std::string> Actions; std::uint64_t StateHash=0u; };
    class DeterministicReplay final
    {
    public:
        void Record(ReplayFrame frame)
        {
            if(!m_Frames.empty()&&frame.Tick<=m_Frames.back().Tick) throw std::invalid_argument("Replay ticks must be strictly increasing.");
            if(m_Frames.size()>=1000000u) throw std::length_error("Replay exceeds one million frames.");
            if(frame.Actions.size()>4096u) throw std::length_error("Replay frame exceeds its action budget.");
            for(const auto& action:frame.Actions) if(action.empty()||action.size()>1024u)
                throw std::invalid_argument("Replay actions must be non-empty and at most 1024 bytes.");
            m_Frames.push_back(std::move(frame));
        }
        [[nodiscard]] const ReplayFrame& Frame(std::size_t index) const { return m_Frames.at(index); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Frames.size(); }
        void Verify(std::size_t index,const RuntimeStateSnapshot& state) const
        { if(HashRuntimeState(state)!=m_Frames.at(index).StateHash) throw std::runtime_error("Replay state diverged from its recorded hash."); }
    private: std::vector<ReplayFrame> m_Frames;
    };
}
