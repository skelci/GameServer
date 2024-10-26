# Service Notes

## Service Ids

- 0 = voice
- 1 = chat
- 2 = world
- 3 = auth
- 4 = control

## Auth Commands

### Step1

- "LOGIN_PREPARE"(username, password) -> "WRONG_PASSWORD" | "WRONG_USERNAME" | "SUCCESS" | "PREPARE_SUCCESS" | "EMAIL_SEND_ERROR" | "ERROR"
- "REGISTER_PREPARE(username, password, email)" -> "USERNAME_EXISTS" | "EMAIL_EXISTS" | "PREPARE_SUCCESS" | "EMAIL_SEND_ERROR" | "ERROR"

### Step2

- "REGISTER"(emailcode) -> "EMAIL_CODE_ATTEMPTS_EXCEEDED" | "EMAIL_CODE_INCORRECT" | "SUCESS" | "ERROR" | "RELOGIN_TOKEN"(token)
- "LOGIN"(emailcode) -> "EMAIL_CODE_ATTEMPTS_EXCEEDED" | "EMAIL_CODE_INCORRECT" | "RELOGIN_TOKEN"(token) | "ERROR"
