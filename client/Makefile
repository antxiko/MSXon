#==============================================================================
# Makefile — MSX Online Client
# Requiere: SDCC + MSXgl
#==============================================================================

# ── Configuración ─────────────────────────────────────────────────────────────
APP      = msxonline
TARGET   = $(APP).com           # Binario MSX-DOS

# Ruta a tu instalación de MSXgl
# Ajusta MSXGL_PATH a donde tengas el repositorio clonado
MSXGL_PATH ?= $(HOME)/msxgl

# ── Herramientas ──────────────────────────────────────────────────────────────
CC       = sdcc
HEXTOCOM = hex2bin          # o ihx2bin según tu instalación
OBJCOPY  = objcopy

# ── Flags de compilación ──────────────────────────────────────────────────────
CFLAGS  = -mz80
CFLAGS += --opt-code-size       # Optimizar para tamaño (Z80 tiene poca RAM)
CFLAGS += --std-sdcc11
CFLAGS += --no-std-crt0         # MSXgl provee su propio crt0

# ── Includes ──────────────────────────────────────────────────────────────────
INCLUDES  = -I.
INCLUDES += -I$(MSXGL_PATH)/engine/src
INCLUDES += -I$(MSXGL_PATH)/engine/src/core
INCLUDES += -I$(MSXGL_PATH)/engine/src/vdp
INCLUDES += -I$(MSXGL_PATH)/engine/src/input
INCLUDES += -I$(MSXGL_PATH)/engine/src/print
INCLUDES += -I$(MSXGL_PATH)/engine/src/network

# ── Librería y objetos MSXgl precompilados ───────────────────────────────────
MSXGL_OUT = $(MSXGL_PATH)/projects/msxonline/out
MSXGL_LIB = $(MSXGL_OUT)/msxgl.lib
CRT0_REL  = $(MSXGL_OUT)/crt0_dos.rel
UNAPI_REL = $(MSXGL_OUT)/unapi_tcp.rel

# ── Fuentes ───────────────────────────────────────────────────────────────────
SRCS     = msxonline.c

# ── Archivos intermedios ──────────────────────────────────────────────────────
OBJS     = $(SRCS:.c=.rel)
IHX      = $(APP).ihx       # Intel HEX generado por SDCC

# ══════════════════════════════════════════════════════════════════════════════
# Reglas
# ══════════════════════════════════════════════════════════════════════════════

.PHONY: all clean

all: $(TARGET)
	@echo ""
	@echo "  ✓ Compilado: $(TARGET)"
	@echo "  Copia $(TARGET) a un disco MSX-DOS y ejecuta:"
	@echo "  A> MSXONLINE"
	@echo ""

# Compilar fuentes → .rel (formato objeto SDCC)
%.rel: %.c protocol.h network.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Enlazar → .ihx
$(IHX): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) \
		$(CRT0_REL) \
		$(MSXGL_LIB) \
		$(UNAPI_REL) \
		$(OBJS) \
		-o $@

# Convertir .ihx → .com (makebin de SDCC, -s 0x0100 = start MSX-DOS)
$(TARGET): $(IHX)
	makebin -s 0x10000 $(IHX) > $(APP).bin
	dd if=$(APP).bin of=$(TARGET) bs=1 skip=256 2>/dev/null
	@rm -f $(APP).bin
	@ls -lh $(TARGET)

clean:
	rm -f *.rel *.sym *.rst *.lst *.lk *.map *.noi *.ihx *.asm *.com

# ── Ayuda ─────────────────────────────────────────────────────────────────────
help:
	@echo "Targets disponibles:"
	@echo "  make          → Compilar msxonline.com"
	@echo "  make clean    → Limpiar archivos intermedios"
	@echo ""
	@echo "Variables configurables:"
	@echo "  MSXGL_PATH=/ruta/a/msxgl make"
