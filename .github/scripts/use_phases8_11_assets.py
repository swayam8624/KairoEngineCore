from pathlib import Path
p = Path('CMakeLists.txt')
s = p.read_text().replace(
    'set(KAIRO_ENGINE_CORE_ASSETS_REVISION 32f9b516298d325730f8551a8ca3451f2009cd2e)',
    'set(KAIRO_ENGINE_CORE_ASSETS_REVISION agent/phases8-11-authoring-completion)')
p.write_text(s)
Path('.github/workflows/use-phases8-11-assets.yml').unlink()
Path('.github/scripts/use_phases8_11_assets.py').unlink()
