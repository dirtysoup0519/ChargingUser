#pragma once

enum class SubmitState {
    Idle,
    Loading,
    Success,
    ValidationError,
    NetworkError,
    ServerError,
    ResultUnknown
};
