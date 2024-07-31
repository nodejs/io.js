
const maxRetry = 3

class GitError extends Error {
  shouldRetry () {
    return false
  }
}

class GitConnectionError extends GitError {
  constructor () {
    super('A git connection error occurred')
  }

  shouldRetry (number) {
    return number < maxRetry
  }
}

class GitPathspecError extends GitError {
  constructor () {
    super('The git reference could not be found')
  }
}

class GitUnknownError extends GitError {
  constructor () {
    super('An unknown git error occurred')
  }
}

module.exports = {
  GitConnectionError,
  GitPathspecError,
  GitUnknownError,
}
