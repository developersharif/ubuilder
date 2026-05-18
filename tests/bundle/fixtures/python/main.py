import sys

print("ubuilder:python:hello")
print("argv:" + ",".join(sys.argv[1:]))
print("sum:" + str(sum(range(1, 11))))
