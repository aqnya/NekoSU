mod proto {
    include!(concat!(env!("OUT_DIR"), "/me.nekosu.aqnya.proto.rs"));

    use prost::Message;

    #[derive(Debug)]
    pub struct Error(prost::DecodeError);

    impl std::fmt::Display for Error {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "proto decode failed: {}", self.0)
        }
    }

    impl std::error::Error for Error {}

    macro_rules! impl_codec {
        ($($t:ty),* $(,)?) => {
            $(
                impl $t {
                    pub fn to_bytes(&self) -> Vec<u8> {
                        self.encode_to_vec()
                    }

                    pub fn from_bytes(bytes: &[u8]) -> Result<Self, $crate::proto::Error> {
                        Self::decode(bytes).map_err($crate::proto::Error)
                    }
                }
            )*
        };
    }

    impl_codec!(ProfileEntry,);
}

pub use proto::*;
