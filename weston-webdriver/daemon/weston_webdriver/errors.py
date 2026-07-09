"""W3C WebDriver error model.

https://www.w3.org/TR/webdriver2/#errors
"""

from __future__ import annotations


class WebDriverError(Exception):
    """An error with a spec-defined JSON error code and HTTP status."""

    def __init__(self, error: str, message: str, http_status: int):
        super().__init__(message)
        self.error = error
        self.message = message
        self.http_status = http_status

    def to_json(self) -> dict:
        return {
            "value": {
                "error": self.error,
                "message": self.message,
                "stacktrace": "",
            }
        }


def invalid_argument(message: str) -> WebDriverError:
    return WebDriverError("invalid argument", message, 400)


def invalid_session_id(session_id: str) -> WebDriverError:
    return WebDriverError(
        "invalid session id", f"no active session with id {session_id!r}", 404
    )


def no_such_window(message: str = "no such window") -> WebDriverError:
    return WebDriverError("no such window", message, 404)


def unknown_command(message: str) -> WebDriverError:
    return WebDriverError("unknown command", message, 404)


def unsupported_operation(message: str) -> WebDriverError:
    return WebDriverError("unsupported operation", message, 500)


def session_not_created(message: str) -> WebDriverError:
    return WebDriverError("session not created", message, 500)


def timeout_error(message: str) -> WebDriverError:
    return WebDriverError("timeout", message, 500)


def unknown_error(message: str) -> WebDriverError:
    return WebDriverError("unknown error", message, 500)
