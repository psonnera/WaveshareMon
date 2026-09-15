"""Compare every values-<lang>/strings.xml with the English base: missing keys, extra keys,
placeholder mismatches and unescaped apostrophes.

    python Scripts/check_translations.py           (run from Android/xDripOBB)
"""
import glob, os, re, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, "..", "app", "src", "main", "res")

def load(path):
    out = {}
    root = ET.parse(path).getroot()
    for s in root.findall("string"):
        name = s.get("name")
        text = "".join(s.itertext())
        out[name] = (text, s.get("translatable", "true") != "false")
    return out

def placeholders(text):
    return sorted(re.findall(r"%(?:\d+\$)?[sdf.0-9]*[sdf]|%%", text))

def main():
    base = load(os.path.join(RES, "values", "strings.xml"))
    need = {k for k, (t, tr) in base.items() if tr}
    ok = True
    for path in sorted(glob.glob(os.path.join(RES, "values-*", "strings.xml"))):
        lang = os.path.basename(os.path.dirname(path))
        tr = load(path)
        missing = sorted(need - set(tr)); extra = sorted(set(tr) - set(base))
        untrans = sorted(k for k in tr if k in base and not base[k][1])
        raw = open(path, encoding="utf-8").read()
        bad_apos = []
        for m in re.finditer(r'<string name="([^"]+)"[^>]*>(?!<!\[CDATA\[)(.*?)</string>', raw, re.S):
            body = m.group(2)
            if re.search(r"(?<!\\)'", body): bad_apos.append(m.group(1))
        # aapt2 rejects a plain apostrophe inside CDATA too: use the typographic one (U+2019)
        cdata_apos = [m.group(1) for m in re.finditer(r'<string name="([^"]+)"[^>]*><!\[CDATA\[(.*?)\]\]>', raw, re.S)
                      if "'" in m.group(2)]
        ph = [k for k in tr if k in base and placeholders(tr[k][0]) != placeholders(base[k][0])]
        problems = [("missing", missing), ("extra", extra), ("non-translatable copied", untrans),
                    ("unescaped apostrophe", bad_apos), ("plain apostrophe inside CDATA", cdata_apos),
                    ("placeholder mismatch", ph)]
        print(f"{lang}: {len(tr)} strings, base needs {len(need)}")
        for label, items in problems:
            if items: ok = False; print(f"  {label}: {', '.join(items)}")
    print("OK" if ok else "PROBLEMS FOUND")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
