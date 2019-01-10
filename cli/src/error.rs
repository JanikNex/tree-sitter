use std::io;

#[derive(Debug)]
pub struct Error(pub String);

pub type Result<T> = std::result::Result<T, Error>;

impl Error {
    pub fn grammar(message: &str) -> Self {
        Error(format!("Grammar error: {}", message))
    }

    pub fn regex(message: &str) -> Self {
        Error(format!("Regex error: {}", message))
    }

    pub fn undefined_symbol(name: &str) -> Self {
        Error(format!("Undefined symbol `{}`", name))
    }
}

impl From<serde_json::Error> for Error {
    fn from(error: serde_json::Error) -> Self {
        Error(error.to_string())
    }
}

impl From<io::Error> for Error {
    fn from(error: io::Error) -> Self {
        Error(error.to_string())
    }
}

impl From<rsass::Error> for Error {
    fn from(error: rsass::Error) -> Self {
        Error(error.to_string())
    }
}

impl From<String> for Error {
    fn from(error: String) -> Self {
        Error(error)
    }
}
