from pathlib import Path

# Persisted decimal text must work on deployment targets where libc++ floating
# from_chars is unavailable, and EOF after std::ws must not be treated as a
# failed numeric extraction.
for name in ['NativeGameplayManifest.cppm', 'ProductionSystemsManifest.cppm']:
    p = Path(name)
    s = p.read_text()
    s = s.replace(
'''            stream >> value;\n            stream >> std::ws;\n            if (!stream || !stream.eof() || !std::isfinite(value))''',
'''            stream >> value;\n            if (stream.fail() || !std::isfinite(value))''')
    s = s.replace(
'''            stream >> value >> std::ws;\n            if (!stream || !stream.eof() || !std::isfinite(value))''',
'''            stream >> value;\n            if (stream.fail() || !std::isfinite(value))''')
    # After the first failure check, reject trailing non-whitespace without
    # considering eofbit itself an extraction failure.
    old = '''                throw NativeGameplayManifestFormatError(line, token.Column,\n                    std::string(field) + " must be a finite decimal number");\n            return value;'''
    new = '''                throw NativeGameplayManifestFormatError(line, token.Column,\n                    std::string(field) + " must be a finite decimal number");\n            stream >> std::ws;\n            if (!stream.eof())\n                throw NativeGameplayManifestFormatError(line, token.Column,\n                    std::string(field) + " must be a finite decimal number");\n            return value;'''
    if name == 'NativeGameplayManifest.cppm':
        s = s.replace(old, new)
    old2 = '''                throw std::invalid_argument("Production manifest line " + std::to_string(line) +\n                    " has invalid " + std::string(field) + ".");\n            return value;'''
    new2 = '''                throw std::invalid_argument("Production manifest line " + std::to_string(line) +\n                    " has invalid " + std::string(field) + ".");\n            stream >> std::ws;\n            if (!stream.eof())\n                throw std::invalid_argument("Production manifest line " + std::to_string(line) +\n                    " has invalid " + std::string(field) + ".");\n            return value;'''
    if name == 'ProductionSystemsManifest.cppm':
        s = s.replace(old2, new2)
    p.write_text(s)

p = Path('SceneSerialization.cppm')
s = p.read_text()
old = '''        [[nodiscard]] inline float ParseFloat(const Token& token, std::size_t line, std::string_view field)\n        {\n            float result = 0.0f;\n            const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), result);\n            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || !std::isfinite(result))\n                throw SceneFormatError(line, token.Column, std::string(field) + " must be a finite float");\n            return result;\n        }'''
new = '''        [[nodiscard]] inline float ParseFloat(const Token& token, std::size_t line, std::string_view field)\n        {\n            std::istringstream stream(token.Text);\n            stream.imbue(std::locale::classic());\n            float result = 0.0f;\n            stream >> result;\n            if (stream.fail() || !std::isfinite(result))\n                throw SceneFormatError(line, token.Column, std::string(field) + " must be a finite float");\n            stream >> std::ws;\n            if (!stream.eof())\n                throw SceneFormatError(line, token.Column, std::string(field) + " must be a finite float");\n            return result;\n        }'''
if old not in s:
    raise SystemExit('SceneSerialization ParseFloat pattern not found')
s = s.replace(old, new)
# Ensure the implementation imports its direct stream/locale dependencies.
if '#include <sstream>' not in s:
    s = s.replace('#include <string>', '#include <locale>\n#include <sstream>\n#include <string>')
elif '#include <locale>' not in s:
    s = s.replace('#include <sstream>', '#include <locale>\n#include <sstream>')
p.write_text(s)

p = Path('tests/NativeGameplayManifestTests.cpp')
s = p.read_text()
if '#include <variant>' not in s:
    s = s.replace('#include <string>\n', '#include <string>\n#include <variant>\n')
p.write_text(s)

Path('.github/workflows/fix-numeric-portability.yml').unlink()
Path('.github/scripts/fix_numeric_portability.py').unlink()
