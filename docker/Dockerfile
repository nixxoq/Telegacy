FROM i386/debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive \
    WINEPREFIX=/root/.wine \
    WINEDEBUG=-all \
    WINEARCH=win32

RUN apt-get update && apt-get install -y --no-install-recommends \
    wine \
    nsis \
    xvfb \
    xauth \
    curl \
    ca-certificates \
    git \
    zip \
    unzip \
    tar \
    && rm -rf /var/lib/apt/lists/*

RUN wineboot -u && wineserver -w

ARG TOOLCHAIN_URL="https://webdav.nixxo.net/stuff/vc6-toolchain.tar.gz"
RUN curl -fsSL -o /tmp/vc6-toolchain.tar.gz "${TOOLCHAIN_URL}" \
    && tar -xzf /tmp/vc6-toolchain.tar.gz -C /tmp/ \
    && cp -a /tmp/drive_c/* /root/.wine/drive_c/ \
    && cp -a /tmp/wine_reg/* /root/.wine/ \
    && rm -rf /tmp/vc6-toolchain.tar.gz /tmp/drive_c /tmp/wine_reg

RUN curl -fsSL -o /tmp/libs.zip https://webdav.nixxo.net/stuff/libs.zip \
    && unzip -q /tmp/libs.zip -d /root/.wine/drive_c/ \
    && rm -f /tmp/libs.zip

RUN mkdir -p /root/.wine/drive_c/windows/temp \
    && for udir in /root/.wine/drive_c/users/*; do \
        [ -d "$udir" ] && mkdir -p "$udir/AppData/Local/Temp" || true; \
    done

RUN wineserver -k || true

WORKDIR /workspace

CMD ["/bin/bash"]
