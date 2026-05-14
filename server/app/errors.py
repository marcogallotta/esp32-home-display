class AppError(Exception):
    pass


class BadRequestError(AppError):
    pass


class UnauthorizedError(AppError):
    pass


class ServerMisconfiguredError(AppError):
    pass
