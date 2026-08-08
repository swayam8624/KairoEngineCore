from pathlib import Path
p=Path('InputMap.cppm')
s=p.read_text()
if '#include <locale>' not in s:
    s=s.replace('#include <iterator>\n','#include <iterator>\n#include <locale>\n')
old='''        [[nodiscard]] inline float ParseFloat(const FormatToken& token)
        {
            float value = 0.0f;
            const auto [end, error] = std::from_chars(token.Text.data(),
                token.Text.data() + token.Text.size(), value, std::chars_format::general);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || !std::isfinite(value))
                throw std::invalid_argument("expected a finite decimal number");
            return value;
        }'''
new='''        [[nodiscard]] inline float ParseFloat(const FormatToken& token)
        {
            std::istringstream stream(std::string(token.Text));
            stream.imbue(std::locale::classic());
            float value = 0.0f;
            stream >> value;
            if (!stream || !std::isfinite(value))
                throw std::invalid_argument("expected a finite decimal number");
            stream >> std::ws;
            if (!stream.eof())
                throw std::invalid_argument("expected a finite decimal number");
            return value;
        }'''
if old not in s: raise SystemExit('InputMap ParseFloat pattern not found')
p.write_text(s.replace(old,new))
Path('.github/workflows/fix-input-float-parser.yml').unlink()
Path('.github/scripts/fix_input_float_parser.py').unlink()
