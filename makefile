# ----------------------------
# Set NAME to the program name
# Set ICON to the png icon file name
# Set DESCRIPTION to display within a compatible shell
# Set COMPRESSED to "YES" to create a compressed program
# ----------------------------

NAME        ?= IC3CRAFT
COMPRESSED  ?= NO
ICON        ?= iconc.png
DESCRIPTION ?= "IcyCraft version 3.0"

# ----------------------------

include $(CEDEV)/include/.makefile
