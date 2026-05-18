import sys
import attrs

print("ubuilder:m8:hello")
print("argv:" + ",".join(sys.argv[1:]))
print("attrs:" + attrs.__version__)
