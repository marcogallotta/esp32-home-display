class AppError(Exception):
    pass


class BadRequestError(AppError):
    pass


class ServerMisconfiguredError(AppError):
    pass
