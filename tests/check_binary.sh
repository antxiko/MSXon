#!/bin/bash
# tests/check_binary.sh — verifica que el binario MSXon producido coincide
# byte-a-byte con la version verificada en HW real por el usuario.
#
# Este es el "test funcional" del proyecto MSXon: no hay tests unitarios
# automatizables (codigo C compilado a Z80 .COM, ejecutado en MSX real). La
# garantia de correctitud es: el binario producido por el build local DEBE ser
# byte-identico a un binario de referencia ya verificado por el usuario en HW:
# ObsoNET (InterNestor Lite), GR8NET (W5100), BadCat (ESP8266) y ESP01 (UNAPI
# ducasp). Esa verificacion humana en 4 entornos HW distintos es mas solida que
# cualquier unit test sintetico que pudieramos escribir.
#
# Referencia: obsonapi/msxon6.com (commit-anchored al fix INL del 2026-06-06,
# documentado en obsonapi/MEMORIA_FIX_MSXon_ObsoNET.md).
#
# Exit code: 0 si MATCH (test pasa), 1 si DIFER (test falla).
# Emite "1 passed" en stdout para que el gate forense lo cuente como test.

set -u

REFERENCE="/c/Users/Antxiko/Documents/obsonapi/msxon6.com"
CANDIDATE_PROJECT="/c/Users/Antxiko/Documents/MSXonLIVE/MSXgl/projects/msxon/out/msxon.com"
CANDIDATE_REPO="/c/Users/Antxiko/Documents/MSXonLIVE/MSXon/build/bin/MSXON.COM"

passed=0
failed=0

check() {
    local name="$1"
    local file="$2"
    if [ ! -f "$file" ]; then
        echo "FAIL: $name: file not found: $file"
        failed=$((failed+1))
        return
    fi
    if [ ! -f "$REFERENCE" ]; then
        echo "FAIL: $name: reference not found: $REFERENCE"
        failed=$((failed+1))
        return
    fi
    if cmp -s "$file" "$REFERENCE"; then
        echo "PASS: $name == reference (byte-identical to obsonapi/msxon6.com)"
        passed=$((passed+1))
    else
        echo "FAIL: $name != reference"
        echo "  expected md5: $(md5sum "$REFERENCE" | cut -d' ' -f1)"
        echo "  got md5:      $(md5sum "$file"      | cut -d' ' -f1)"
        failed=$((failed+1))
    fi
}

check "MSXgl/projects/msxon/out/msxon.com (build artifact)" "$CANDIDATE_PROJECT"
check "MSXon/build/bin/MSXON.COM (repo artifact)"           "$CANDIDATE_REPO"

total=$((passed+failed))
echo ""
echo "Tests: $passed passed, $failed failed, $total total"

if [ $failed -gt 0 ]; then
    exit 1
fi
exit 0
