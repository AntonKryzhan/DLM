module native.versioned_state
import std.native_binary
native {
    // Schema 1: gain (1), UTF-8 name (2). Schema 2 adds enabled (3).
    // The original envelope carries unknown fields, flags and ordering.
    record Settings repr(C) { gain: F64; name: Buffer<U8>; enabled: Bool; wire: Buffer<U8>; valid: Bool; }
    fn settings_read(input: Slice<U8>) -> Settings {
        let mut bytes: Buffer<U8>=buffer<U8>(len(input));for i in 0..len(input) { bytes[i]=input[i]; }
        let mut name: Buffer<U8>=buffer<U8>(0);let mut gain: F64=1.0;let mut enabled: Bool=true;
        if !wire_valid(&bytes) { return Settings(gain,move name,enabled,move bytes,false); }
        let mut seen: U32=0;let mut p: USize=8;
        while p < len(&bytes) {
            let id: U32=read_le<U32>(&bytes,p);let count: USize=cast<USize>(read_le<U32>(&bytes,p+4));
            if id >= U32(1) && id <= U32(3) {
                let mask: U32=U32(1)<<id;if (seen&mask) != U32(0) { return Settings(gain,move name,enabled,move bytes,false); }seen=seen|mask;
                if id == U32(1) { if count != 8 { return Settings(gain,move name,enabled,move bytes,false); }gain=read_le<F64>(&bytes,p+8); }
                if id == U32(2) {
                    let text: Slice<U8>=slice(&input,p+8,count);let mut offset: USize=0;
                    while offset < count { let step: Utf8Step=utf8_next(text,offset);if !step.valid { return Settings(gain,move name,enabled,move bytes,false); }offset=step.next; }
                    resize(&mut name,count);for i in 0..count { name[i]=text[i]; }
                }
                if id == U32(3) { if count != 1 || bytes[p+8] > U8(1) { return Settings(gain,move name,enabled,move bytes,false); }enabled=bytes[p+8] == U8(1); }
            }
            p=p+8+count;
        }
        return Settings(gain,move name,enabled,move bytes,true);
    }
    fn settings_write(settings: &Settings) -> Buffer<U8> {
        assert(settings.valid);let mut bytes: Buffer<U8>=buffer<U8>(len(&settings.wire));for i in 0..len(&settings.wire) { bytes[i]=settings.wire[i]; }
        let mut number: Array<U8,8>=zero<Array<U8,8>>();write_le<F64>(&mut number,0,settings.gain);wire_set(&mut bytes,1,slice(&number,0,8));
        wire_set(&mut bytes,2,slice(&settings.name,0,len(&settings.name)));
        let mut enabled: Array<U8,1>=array<Array<U8,1>>(U8(0));if settings.enabled { enabled[0]=1; }wire_set(&mut bytes,3,slice(&enabled,0,1));wire_upgrade(&mut bytes);return move bytes;
    }
    export unsafe fn settings_transform(input: Ptr<U8>, count: USize, output: Ptr<U8>, capacity: USize, gain: F64) -> USize {
        let source: ForeignBuffer<U8>=foreign(input,count);let mut settings: Settings=settings_read(slice(&source,0,count));if !settings.valid { return 0; }settings.gain=gain;
        let bytes: Buffer<U8>=settings_write(&settings);if len(&bytes)>capacity { return 0; }
        let mut target: ForeignBuffer<U8>=foreign(output,capacity);for i in 0..len(&bytes) { target[i]=bytes[i]; }return len(&bytes);
    }
    fn main()->Unit {
        let mut old: Buffer<U8>=wire_new(1);let mut number: Array<U8,8>=zero<Array<U8,8>>();write_le<F64>(&mut number,0,0.25);wire_set(&mut old,1,slice(&number,0,8));wire_set(&mut old,2,utf8_bytes("Антон"));wire_set(&mut old,900,utf8_bytes("future payload"));
        let mut settings: Settings=settings_read(slice(&old,0,len(&old)));assert(settings.valid);assert(settings.enabled);assert(settings.gain==0.25);settings.gain=0.75;
        let encoded: Buffer<U8>=settings_write(&settings);assert(read_le<U16>(&encoded,4)==U16(2));let unknown: Slice<U8>=wire_field(&encoded,900);assert(len(unknown)==USize(14));
        let again: Settings=settings_read(slice(&encoded,0,len(&encoded)));assert(again.valid);assert(again.gain==0.75);assert(len(&again.name)==USize(10));
        let bad: Settings=settings_read(utf8_bytes("corrupt"));assert(!bad.valid);print("versioned settings OK");
    }
}
