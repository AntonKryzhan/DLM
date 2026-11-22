module native.ordinary_bridge

theory Math {
    record OldPair(left: Float, right: Nat)
    let offset = fl_of_nat(3)
    fn affine(x: Float) -> Float = fl_add(fl_mul(x, fl_of_nat(2)), offset)
    fn identity_text(x: Text) -> Text = x
    fn identity_pair(x: OldPair) -> OldPair = x
    record OldBundle(samples: FloatVec, count: Nat, name: Text)
    fn identity_bundle(x: OldBundle) -> OldBundle = x
    fn identity_naturals(x: NatVec) -> NatVec = x
    fn identity_bytes(x: Text) -> Text = x
    fn too_large() -> Nat = 256
}

native {
    record Pair repr(C) { left: F64; right: U32; }
    record Bundle repr(C) { samples: Buffer<F32>; count: U32; name: Buffer<U8>; }
    ordinary "Math.identity_bundle" fn bundle(x: &Bundle) -> Bundle;
    ordinary "Math.identity_naturals" fn naturals(x: &Array<U16,3>) -> Array<U16,3>;
    ordinary "Math.identity_bytes" fn bytes(x: &Buffer<U8>) -> Buffer<U8>;
    ordinary "Math.affine" fn affine(x: F64) -> F64;
    ordinary "Math.identity_text" fn text(x: Utf8) -> Buffer<U8>;
    ordinary "Math.identity_pair" fn pair(x: Pair) -> Pair;
    ordinary "Math.too_large" fn narrow() -> U8;
    export fn apply(x: F64) -> F64 { return affine(x); }
    export fn narrowing_error() -> U8 { return narrow(); }
    export fn invalid_utf8() -> USize {
        let mut input: Buffer<U8>=buffer<U8>(1);input[0]=255;
        let output: Buffer<U8>=bytes(&input);return len(&output);
    }
    fn main() -> Unit {
        assert(affine(4.0) == 11.0);
        let value: Pair = pair(Pair(1.5, U32(9)));
        assert(value.left == 1.5); assert(value.right == U32(9));
        let bytes: Buffer<U8> = text("Привет & DLM");
        assert(len(&bytes) == USize(18));
        let mut samples: Buffer<F32>=buffer<F32>(2);samples[0]=F32(3.5);samples[1]=F32(-0.0);
        let original: Bundle=Bundle(move samples,U32(42),text("Антон"));
        let mut result: Bundle=bundle(&original);assert(result.count==U32(42));assert(len(&result.name)==USize(10));assert(bits(result.samples[1])==U32(0x80000000));
        result.samples[0]=F32(8);assert(original.samples[0]==F32(3.5));
        let numbers: Array<U16,3>=array<Array<U16,3>>(U16(0),U16(42),U16(65535));let copy: Array<U16,3>=naturals(&numbers);assert(copy[2]==U16(65535));
        print("ordinary bridge OK");
    }
}
