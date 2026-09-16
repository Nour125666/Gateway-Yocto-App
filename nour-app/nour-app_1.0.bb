SUMMARY = "Nour embedded sensor communication application"
DESCRIPTION = "Nour sensor daemon, TCP/MQTT agent and command-line interface"

LICENSE = "CLOSED"


# ============================================================
# SOURCE FILES
# ============================================================

SRC_URI = " \
    file://sensord.c \
    file://agent.cpp \
    file://cli.c \
    file://nour-agent.conf \
    file://nour-agent-start \
    file://nour-sensord.service \
    file://nour-agent.service \
"
inherit systemd

# The source files are unpacked into WORKDIR.
S = "${WORKDIR}"


# ============================================================
# BUILD DEPENDENCIES
# ============================================================

# agent.cpp uses the Eclipse Paho MQTT C and C++ libraries.
DEPENDS = " \
    paho-mqtt-c \
    paho-mqtt-cpp \
"


# ============================================================
# COMPILE
# ============================================================

do_compile() {

    # --------------------------------------------------------
    # sensord
    #
    # Uses:
    #   pthread
    #   Unix sockets
    #   termios UART
    # --------------------------------------------------------

    ${CC} ${CFLAGS} \
        ${S}/sensord.c \
        -o sensord \
        ${LDFLAGS} \
        -pthread


    # --------------------------------------------------------
    # agent
    #
    # C++ application using Eclipse Paho MQTT.
    # --------------------------------------------------------

    ${CXX} ${CXXFLAGS} \
        ${S}/agent.cpp \
        -o agent \
        ${LDFLAGS} \
        -pthread \
        -lpaho-mqttpp3 \
        -lpaho-mqtt3a


    # --------------------------------------------------------
    # CLI
    # --------------------------------------------------------

    ${CC} ${CFLAGS} \
        ${S}/cli.c \
        -o cli \
        ${LDFLAGS}
}


# ============================================================
# INSTALL
# ============================================================

do_install() {

    # ${bindir} normally corresponds to /usr/bin
    install -d ${D}${bindir}


    install -m 0755 \
        sensord \
        ${D}${bindir}/sensord


    install -m 0755 \
        agent \
        ${D}${bindir}/agent


    install -m 0755 \
        cli \
        ${D}${bindir}/cli
    # --------------------------------------------------------
    # Nour configuration
    # --------------------------------------------------------

    install -d ${D}${sysconfdir}/nour

    install -m 0644 \
        ${S}/nour-agent.conf \
        ${D}${sysconfdir}/nour/nour-agent.conf


    # --------------------------------------------------------
    # Agent startup helper
    # --------------------------------------------------------

    install -m 0755 \
        ${S}/nour-agent-start \
        ${D}${bindir}/nour-agent-start


    # --------------------------------------------------------
    # systemd services
    # --------------------------------------------------------

    install -d \
        ${D}${systemd_system_unitdir}


    install -m 0644 \
        ${S}/nour-sensord.service \
        ${D}${systemd_system_unitdir}/nour-sensord.service


    install -m 0644 \
        ${S}/nour-agent.service \
        ${D}${systemd_system_unitdir}/nour-agent.service
}
SYSTEMD_SERVICE:${PN} = " \
    nour-sensord.service \
    nour-agent.service \
"

SYSTEMD_AUTO_ENABLE:${PN} = "enable"
