module examples.native.network_crypto
import std.net_crypto
import std.net_async

native {
    fn main() -> Unit {
        assert(crypto_available());
        let alice: CryptoPrivate=crypto_private_generate();let bob: CryptoPrivate=crypto_private_generate();
        let ea: NetworkError=crypto_private_error(&alice);let eb: NetworkError=crypto_private_error(&bob);assert(network_ok(&ea) && network_ok(&eb));
        let mut a: Buffer<U8>=buffer<U8>(32);let mut b: Buffer<U8>=buffer<U8>(32);
        let pa: NetworkTransfer=crypto_public(&alice,&mut a);let pb: NetworkTransfer=crypto_public(&bob,&mut b);assert(network_ok(&pa.error) && network_ok(&pb.error));
        let left: CryptoSecret=crypto_agree(move alice,slice(&b,0,32));let right: CryptoSecret=crypto_agree(move bob,slice(&a,0,32));
        let el: NetworkError=crypto_secret_error(&left);let er: NetworkError=crypto_secret_error(&right);assert(network_ok(&el) && network_ok(&er));
        let mut transcript: Buffer<U8>=buffer<U8>(64);
        for i in 0..32 { transcript[i]=a[i];transcript[i+32]=b[i]; }
        let mut client: CryptoChannel=crypto_channel_open(move left,slice(&a,0,32),slice(&transcript,0,64),crypto_initiator());
        let mut server: CryptoChannel=crypto_channel_open(move right,slice(&a,0,32),slice(&transcript,0,64),crypto_responder());
        let ec: NetworkError=crypto_channel_error(&client);let es: NetworkError=crypto_channel_error(&server);assert(network_ok(&ec) && network_ok(&es));
        let mut message: Buffer<U8>=buffer<U8>(4);message[0]=0;message[1]=255;message[2]=128;message[3]=42;
        let mut record: Buffer<U8>=buffer<U8>(64);
        let sealed: NetworkTransfer=crypto_seal(&mut client,slice(&message,0,4),slice(&b,0,32),&mut record);assert(network_ok(&sealed.error) && sealed.count==36);
        let cancel: Resource<OpaqueHandle,net_cancel_dispose>=network_cancel();
        let mut group: NetworkScope=scope_open(1,2,128,2);
        let udp: UdpSocket=udp_bind(socket_address_v4(127,0,0,1,0),false);
        let address: SocketAddressResult=udp_local(&udp);assert(network_ok(&address.error));
        let adopted: ReactorUdpResult=scope_adopt_udp(&mut group,move udp);assert(network_ok(&adopted.error));
        let reading: NetworkOperation=scope_receive(&mut group,adopted.channel,64,1000);
        let writing: NetworkOperation=scope_send(&mut group,adopted.channel,address.address,slice(&record,0,sealed.count),1000);
        assert(network_ok(&reading.error) && network_ok(&writing.error));
        let joined: NetworkTransfer=scope_join(&mut group,1000,&cancel);assert(network_ok(&joined.error));
        let copied: NetworkTransfer=scope_copy(&group,reading.id,0,&mut record);assert(network_ok(&copied.error) && copied.count==36);
        let mut plaintext: Buffer<U8>=buffer<U8>(8);for i in 0..8 { plaintext[i]=77; }
        record[35]=record[35]^U8(1);
        let bad: NetworkTransfer=crypto_open(&mut server,slice(&record,0,36),slice(&b,0,32),&mut plaintext);assert(crypto_auth_failed(&bad.error));
        for i in 0..8 { assert(plaintext[i]==77); }
        record[35]=record[35]^U8(1);
        let opened: NetworkTransfer=crypto_open(&mut server,slice(&record,0,36),slice(&b,0,32),&mut plaintext);assert(network_ok(&opened.error) && opened.count==4);
        assert(plaintext[0]==0 && plaintext[1]==255 && plaintext[2]==128 && plaintext[3]==42 && plaintext[4]==77);
        let duplicate: NetworkTransfer=crypto_open(&mut server,slice(&record,0,36),slice(&b,0,32),&mut plaintext);assert(crypto_replayed(&duplicate.error));
        let mut digest: Buffer<U8>=buffer<U8>(32);let hashed: NetworkTransfer=crypto_sha256(slice(&message,0,4),&mut digest);assert(network_ok(&hashed.error) && hashed.count==32);
        let stats: CryptoStatistics=crypto_statistics(&server);assert(stats.received==1 && stats.failed_auth==1);
        scope_close(move group);resource_close(move cancel);crypto_channel_close(move client);crypto_channel_close(move server);
        unsafe { let crypto: NetIo=crypto_provider_shutdown();assert(crypto.status==0);let net: NetIo=network_provider_shutdown();assert(net.status==0); }
        print("X25519 HKDF AES-GCM encrypted UDP and replay checks OK");
    }
}
