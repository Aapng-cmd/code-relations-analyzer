import app

a = app.App([1, 2, 3])
print(a.get_options())
a.mutate()
print(a.get_options())
