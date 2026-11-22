module native.foundation
native {
    record Pair repr(C, version=1) {
        count: U32;
        value: F64;
    }
    export fn affine(x: F64, gain: F64, bias: F64) -> F64 guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe) {
        return x * gain + bias;
    }
    fn main() -> Unit {
        let mut values: Buffer<F64> = buffer<F64>(8);
        for i in 0..len(&values) {
            values[i] = affine(cast<F64>(i), 2.0, 1.0);
        }
        let mut sum: F64 = 0.0;
        for i in 0..len(&values) {
            if i == 3 { continue; }
            sum = sum + values[i];
        }
        assert(sum == 57.0);
        let mut p: Pair = Pair(4, sum);
        p.count = p.count + 1;
        assert(p.count == 5);
        let mut i: USize = 0;
        while i < 10 {
            i = i + 1;
            if i == 6 { break; }
        }
        assert(i == 6);
        let mut bytes: Buffer<U8> = buffer<U8>(16);
        write_le<U64>(&mut bytes, 0, 18446744073709551615);
        assert(read_le<U64>(&bytes, 0) == U64(18446744073709551615));
        write_be<F64>(&mut bytes, 8, -0.0);
        assert(sign_bit(read_be<F64>(&bytes, 8)));
        let vector: Simd<F64,4> = simd_load<Simd<F64,4>>(&values, 0);
        let doubled: Simd<F64,4> = simd_add(vector, vector);
        simd_store(&mut values, 0, doubled);
        assert(values[0] == 2.0);
        let counter: AtomicU64 = atomic<U64>(4);
        assert(atomic_compare_exchange(&counter, 4, 5, AcqRel, Acquire));
        assert(atomic_load(&counter, Acquire) == U64(5));
        print("Native foundation OK");
    }
}
