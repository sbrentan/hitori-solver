from enum import Enum

class LogLevel(Enum):
    DEBUG = 0
    INFO = 1
    WARNING = 2
    ERROR = 3

    def __lt__(self, other):
        return self.value < other.value
    
    def __le__(self, other):
        return self.value <= other.value

class Log:
    def __init__(self, level: LogLevel):
        self.level = level

    def debug(self, *args, **kwargs):
        if self.level <= LogLevel.DEBUG:
            print(*args, **kwargs)
    
    def info(self, *args, **kwargs):
        if self.level <= LogLevel.INFO:
            print(*args, **kwargs)

    def warning(self, st):
        if self.level <= LogLevel.WARNING:
            print(st)

    def error(self, st):
        if self.level <= LogLevel.ERROR:
            print(st)
