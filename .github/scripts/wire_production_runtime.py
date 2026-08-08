from pathlib import Path

p=Path('ProductionSystemsManifest.cppm')
s=p.read_text().replace('#include <charconv>', '#include <algorithm>\n#include <charconv>')
insert='''\n    class ProductionSystemsManifestFormatError final : public std::runtime_error\n    {\n    public:\n        ProductionSystemsManifestFormatError(std::size_t line, std::size_t column, std::string message)\n            : std::runtime_error("Kairo production manifest " + std::to_string(line) + ":" +\n                std::to_string(column) + ": " + message), Line(line), Column(column) {}\n        std::size_t Line;\n        std::size_t Column;\n    };\n'''
s=s.replace('    struct ProductionSystemsManifest final\n', insert+'\n    struct ProductionSystemsManifest final\n')
s=s.replace('TokenizeFormatLine<std::invalid_argument>', 'TokenizeFormatLine<ProductionSystemsManifestFormatError>')
p.write_text(s)

p=Path('ProductionRuntime.cppm')
s=p.read_text().replace('#include <cmath>', '#include <algorithm>\n#include <cmath>').replace('#include <cstddef>', '#include <cstddef>\n#include <cstdint>')
p.write_text(s)

p=Path('CMakeLists.txt')
s=p.read_text()
s=s.replace('NativeGameplay.cppm NativeGameplayManifest.cppm ProductionSystems.cppm', 'NativeGameplay.cppm NativeGameplayManifest.cppm ProductionSystems.cppm ProductionSystemsManifest.cppm ProductionRuntime.cppm')
s=s.replace('tests/ProductionSystemsTests.cpp\n        tests/PlatformTests.cpp', 'tests/ProductionSystemsTests.cpp\n        tests/ProductionRuntimeTests.cpp\n        tests/PlatformTests.cpp')
p.write_text(s)

p=Path('KairoEngineCore.cppm')
s=p.read_text().replace('export import Kairo.EngineCore.ProductionSystems;\n', 'export import Kairo.EngineCore.ProductionSystems;\nexport import Kairo.EngineCore.ProductionSystemsManifest;\nexport import Kairo.EngineCore.ProductionRuntime;\n')
p.write_text(s)

Path('.github/workflows/wire-production-runtime.yml').unlink()
Path('.github/scripts/wire_production_runtime.py').unlink()
