module examples.native.network_crypto_limits
import std.net_crypto

native {
    // Deterministic fixture material. Fresh session keys are demonstrated by
    // network_crypto; importing/repeating application keys is explicitly unsafe.
    fn fixture(role: CryptoRole, records: U64, bytes: U64) -> CryptoChannel {
        let mut key: Buffer<U8>=buffer<U8>(32);for i in 0..32 { key[i]=cast<U8>(i); }
        unsafe {
            let secret: CryptoSecret=crypto_secret_import(slice(&key,0,32));
            return crypto_channel_limited(move secret,slice(&key,0,0),slice(&key,0,32),role,records,bytes,10000);
        }
    }
    fn main() -> Unit {
        assert(crypto_available());
        let failed: CryptoSecret=crypto_secret_generate(0);let mut untouched: Buffer<U8>=buffer<U8>(32);
        for i in 0..32 { untouched[i]=77; }
        let empty: Buffer<U8>=buffer<U8>(0);
        let no_key: NetworkTransfer=crypto_hmac(&failed,slice(&empty,0,0),&mut untouched);assert(no_key.error.kind==4);
        for i in 0..32 { assert(untouched[i]==77); }
        crypto_secret_close(move failed);
        let mut sender: CryptoChannel=fixture(crypto_initiator(),2,6);
        let mut receiver: CryptoChannel=fixture(crypto_responder(),2,6);
        let e: NetworkError=crypto_channel_error(&sender);let r: NetworkError=crypto_channel_error(&receiver);assert(network_ok(&e) && network_ok(&r));
        let mut plain: Buffer<U8>=buffer<U8>(3);plain[0]=0;plain[1]=255;plain[2]=42;
        let mut short: Buffer<U8>=buffer<U8>(34);
        let rejected: NetworkTransfer=crypto_seal(&mut sender,slice(&plain,0,3),slice(&plain,0,0),&mut short);assert(rejected.error.kind==7);
        let before: CryptoStatistics=crypto_statistics(&sender);assert(before.sent==0);
        let mut record: Buffer<U8>=buffer<U8>(35);let mut output: Buffer<U8>=buffer<U8>(8);
        for i in 0..2 {
            let sealed: NetworkTransfer=crypto_seal(&mut sender,slice(&plain,0,3),slice(&plain,0,0),&mut record);assert(network_ok(&sealed.error));
            let opened: NetworkTransfer=crypto_open(&mut receiver,slice(&record,0,35),slice(&plain,0,0),&mut output);assert(network_ok(&opened.error) && opened.count==3);
        }
        let exhausted: NetworkTransfer=crypto_seal(&mut sender,slice(&plain,0,3),slice(&plain,0,0),&mut record);assert(crypto_exhausted(&exhausted.error));
        let final: CryptoStatistics=crypto_statistics(&sender);assert(final.sent==2 && final.send_bytes==6);
        crypto_channel_close(move sender);crypto_channel_close(move receiver);
        unsafe { let crypto: NetIo=crypto_provider_shutdown();assert(crypto.status==0);let net: NetIo=network_provider_shutdown();assert(net.status==0); }
        print("crypto key budgets capacity and ownership OK");
    }
}
