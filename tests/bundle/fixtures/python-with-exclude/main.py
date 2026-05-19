import sys
import attrs

# Asserting that `six` is absent from the bundle: importing it must raise
# ImportError because we passed --exclude=six at build time. If the exclude
# pipeline silently no-op'd, this script would print attrs+six and the
# expected.txt comparison would fail.
six_absent = False
try:
    import six  # noqa: F401
except ImportError:
    six_absent = True

print("ubuilder:py-exclude:hello")
print("argv:" + ",".join(sys.argv[1:]))
print("attrs:" + attrs.__version__)
print("six-absent:" + str(six_absent))
