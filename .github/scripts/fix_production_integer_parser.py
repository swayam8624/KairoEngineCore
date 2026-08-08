from pathlib import Path
p=Path('ProductionSystemsManifest.cppm')
s=p.read_text()
old='''            if (error != std::errc{} || end != token.Text.data() + token.Text.size())\n                throw std::invalid_argument("Production manifest line " + std::to_string(line) +\n                    " has invalid " + std::string(field) + ".");\n            stream >> std::ws;\n            if (!stream.eof())\n                throw std::invalid_argument("Production manifest line " + std::to_string(line) +\n                    " has invalid " + std::string(field) + ".");\n            return value;'''
new='''            if (error != std::errc{} || end != token.Text.data() + token.Text.size())\n                throw std::invalid_argument("Production manifest line " + std::to_string(line) +\n                    " has invalid " + std::string(field) + ".");\n            return value;'''
if old not in s: raise SystemExit('integer parser pattern not found')
p.write_text(s.replace(old,new,1))
Path('.github/workflows/fix-production-integer-parser.yml').unlink()
Path('.github/scripts/fix_production_integer_parser.py').unlink()
