CC = gcc
CFLAGS = -Wall -Wextra -std=gnu99 -O2 -D_GNU_SOURCE
LDFLAGS = -ldrm -lm

# Directorios de headers (ajustar según tu sistema)
INCLUDES = -I/usr/include/drm -I/usr/include/libdrm

# Archivos fuente
SRCS = drm_display.c
OBJS = $(SRCS:.c=.o)
TARGET = drm_display

# Archivo header de stb_image (se descarga automáticamente)
STB_IMAGE = stb_image.h

.PHONY: all clean install deps

all: $(TARGET)

$(TARGET): $(OBJS) $(STB_IMAGE)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

%.o: %.c $(STB_IMAGE)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(STB_IMAGE):
	@echo "stb_image.h should be already into the source..."
	# wget -q https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -O $(STB_IMAGE)

# Instalar dependencias en sistemas Debian/Ubuntu
deps-debian:
	sudo apt-get update
	sudo apt-get install -y libdrm-dev build-essential wget

# Instalar dependencias en sistemas Red Hat/CentOS/Fedora
deps-redhat:
	sudo yum install -y libdrm-devel gcc make wget || sudo dnf install -y libdrm-devel gcc make wget

# Instalar dependencias en Alpine Linux (para sistemas embebidos)
deps-alpine:
	sudo apk add libdrm-dev gcc make musl-dev wget

clean:
	rm -f $(OBJS) $(TARGET) $(STB_IMAGE)

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/
	sudo chmod +x /usr/local/bin/$(TARGET)

# Para cross-compilación (ejemplo para ARM)
cross-arm:
	$(MAKE) CC=arm-linux-gnueabihf-gcc LDFLAGS="-ldrm -lm -static"

# Para sistemas embebidos con musl
embedded:
	$(MAKE) CC=gcc CFLAGS="$(CFLAGS) -static" LDFLAGS="-ldrm -lm -static"

help:
	@echo "Objetivos disponibles:"
	@echo "  all          - Compilar el programa"
	@echo "  deps-debian  - Instalar dependencias en Debian/Ubuntu"
	@echo "  deps-redhat  - Instalar dependencias en Red Hat/CentOS/Fedora"
	@echo "  deps-alpine  - Instalar dependencias en Alpine Linux"
	@echo "  clean        - Limpiar archivos compilados"
	@echo "  install      - Instalar en /usr/local/bin"
	@echo "  cross-arm    - Cross-compilar para ARM"
	@echo "  embedded     - Compilar estático para sistemas embebidos"
	@echo "  help         - Mostrar esta ayuda"
	@echo "  svg2png      - Convertir todas las imágenes SVG en images/ a PNG (requiere rsvg-convert)"

# Convertir SVG a PNG en images/ usando rsvg-convert
svg2png:
	@echo "Convirtiendo SVG a PNG en images/ ..."
	@for svg in images/*.svg; do \
	  png="$${svg%.svg}.png"; \
	  if [ ! -f "$$png" ] || [ "$$svg" -nt "$$png" ]; then \
		echo "$$svg -> $$png"; \
		rsvg-convert -o "$$png" "$$svg"; \
	  fi; \
	done
