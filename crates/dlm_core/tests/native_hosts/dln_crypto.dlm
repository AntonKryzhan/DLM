module fixtures.dln.crypto
import std.net_crypto

native {
    record CryptoOwners repr(C) { first: CryptoPrivate; second: CryptoSecret; channel: CryptoChannel; }
    export fn crypto_abort_with_partial_move() -> Unit {
        let secret: CryptoSecret=crypto_secret_generate(32);let context: Buffer<U8>=buffer<U8>(0);
        let channel: CryptoChannel=crypto_channel_open(move secret,slice(&context,0,0),slice(&context,0,0),crypto_initiator());
        let mut owners: CryptoOwners=CryptoOwners(crypto_private_generate(),crypto_secret_generate(32),move channel);
        crypto_private_close(move owners.first);assert(false);
    }
    export unsafe fn crypto_shutdown() -> NetIo { return crypto_provider_shutdown(); }
    export unsafe fn crypto_network_shutdown() -> NetIo { return network_provider_shutdown(); }
}
