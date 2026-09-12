import utils

class App:
    def __init__(self, options):
        self.options = options

    def get_options(self):
        return self.options

    def mutate(self):
        self.options = utils.rotate(self.options)
