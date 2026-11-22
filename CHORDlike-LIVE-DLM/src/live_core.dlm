module chordlike.live_core

native {
    record Stereo repr(C) { left: F64; right: F64; }
    record Fir repr(C) { taps: Buffer<F64>; history: Buffer<F64>; index: USize; size: USize; }
    record Biquad repr(C) {
        b0: F64; b1: F64; b2: F64; a1: F64; a2: F64; z1: F64; z2: F64;
    }
    record Allpass repr(C) { input: F64; output: F64; }
    record Creative repr(C) {
        amounts: Array<F64,3>;
        focusState: Array<F64,2>; warmState: Array<F64,2>; hfState: Array<F64,2>;
        allpass: Array<Allpass,4>;
        smoothing: F64; focusPole: F64; warmPole: F64; hfPole: F64;
        ap0: F64; ap1: F64;
    }
    record Pulse repr(C) {
        ntf: Array<F64,12>; errors: Array<F64,11>;
        prefixes: Array<F64,110>; gains: Array<F64,100>;
        order: USize; elements: USize; position: USize;
        rng: U32; seed: U32; totalGain: F64;
    }
    record Dac repr(C) {
        ntf: Array<F64,12>; errors: Array<F64,11>;
        prefixes: Array<F64,110>; gains: Array<F64,100>;
        order: USize; mode: USize; rotation: USize;
        rng: U32; seed: U32;
    }
    record OutputStage repr(C) {
        signal: Array<F64,256>;
        peakHistory: Array<F64,128>;
        kernels: Array<F64,68>;
        index: USize;
        limiterPreviouslyEnabled: Bool;
        inputPower: F64; processedPower: F64;
        autoGain: F64; limiterGain: F64;
        powerAlpha: F64; matchAlpha: F64; releaseAlpha: F64;
        estimatedPeak: F64; reductionDb: F64; autoGainDb: F64;
    }
    record TubeDef repr(C) {
        mu: F64; ex: F64; kg1: F64; kp: F64; kvb: F64;
        plate: F64; grid: F64; load: F64; screen: F64; swing: F64;
        pentode: Bool;
    }
    record ValveChannel repr(C) {
        oldType: USize; newType: USize;
        oldTopology: USize; newTopology: USize;
        transition: F64; drive: F64; bias: F64; sag: F64; wet: F64; envelope: F64;
    }
    record Tube repr(C) {
        curves: Buffer<F32>;
        channels: Array<ValveChannel,2>;
        desiredType: USize; desiredTopology: USize;
        desiredDrive: F64; desiredBias: F64; desiredSag: F64; desiredWet: F64;
        smoothing: F64; envelopeAttack: F64; envelopeRelease: F64; transitionStep: F64;
    }
    record BandChannel repr(C) {
        filters: Array<Biquad,8>;
        currentLower: F64; currentUpper: F64;
        coefficientCounter: I32;
    }
    record ThreeBand repr(C) {
        stages: Array<Tube,3>;
        channels: Array<BandChannel,2>;
        rate: F64; targetLower: F64; targetUpper: F64;
        frequencySmoothing: F64;
        anyActive: Bool;
        releaseFrames: Array<USize,2>;
        types: Array<USize,3>;
        wets: Array<F64,3>;
    }
    record Folded repr(C) {
        coefficients: Array<F64,2304>;
        lengths: Array<USize,6>;
        history: Array<F64,1536>;
        index: USize;
    }
    record Engine repr(C) {
        up: Buffer<Fir>; down: Buffer<Fir>;
        dryL: Array<F64,157>; dryR: Array<F64,157>;
        alignedL: Array<F64,63>; alignedR: Array<F64,63>;
        delayL: Array<F64,64>; delayR: Array<F64,64>;
        dryIndex: USize; alignIndex: USize; crossIndex: USize;
        crossLowL: F64; crossLowR: F64; crossAlpha: F64; crossSamples: USize;
        crossCurrent: F64; mixCurrent: F64; gainCurrent: F64; controlAlpha: F64;
        warmLowL: F64; warmLowR: F64; warmAlpha: F64;
        warmMix: F64; hfMix: F64; hfAvailable: Bool;
        hfL: Biquad; hfR: Biquad;
        creative: Creative;
        pulses: Array<Pulse,2>; dacs: Array<Dac,2>;
        pulseMode: USize; dacMode: USize; mismatch: F64;
        characterCurrent: F64; pulseColorCurrent: F64;
        depth: USize; sampleRate: F64;
        output: OutputStage;
        tube: Tube; tubeActive: Bool;
        threeBand: ThreeBand;
        tubeRouting: USize; tubeRouteBlend: Array<F64,2>; tubeRouteStep: F64;
        pendingDepth: USize; pendingPulse: USize; pendingDac: USize;
        transitionCounter: USize; transitioning: Bool;
        folded: Folded;
        fastEnabled: Bool; foldedHistoryActive: Bool;
        foldedPriming: USize; legacyPriming: USize;
        legacyRunning: Bool; fastWeight: F64;
        foldedDepth: USize; previousFoldedDepth: USize; foldedFade: USize;
        tubeCpuMode: USize;
    }

    fn min_f(x: F64, lo: F64, hi: F64) -> F64 guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe) {
        if x < lo { return lo; }
        if x > hi { return hi; }
        return x;
    }
    fn pi() -> F64 guarantees(NoAlloc, NoBlocking, NoIO, NoUnwind, BoundedWork, ThreadSafe) {
        return 3.1415926535897932384626433832795;
    }
    fn tube_definition(index: USize) -> TubeDef {
        if index==0 {return TubeDef(100.0,1.40,1060.0,600.0,300.0,180.0,-1.5,100000.0,0.0,1.3,false);}
        if index==1 {return TubeDef(21.5,1.30,1180.0,84.0,300.0,160.0,-8.0,22000.0,0.0,7.0,false);}
        if index==2 {return TubeDef(28.0,1.30,330.0,320.0,300.0,90.0,-2.0,15000.0,0.0,1.7,false);}
        if index==3 {return TubeDef(8.7,1.35,1460.0,48.0,12.0,300.0,-20.0,2500.0,250.0,16.0,true);}
        if index==4 {return TubeDef(7.9,1.35,890.0,60.0,24.0,360.0,-37.0,2500.0,350.0,30.0,true);}
        return TubeDef(8.8,1.35,730.0,32.0,16.0,350.0,-40.0,2300.0,400.0,34.0,true);
    }
    fn tube_softplus(x: F64) -> F64 {
        if x>40.0 {return x;}
        if x< -40.0 {return exp(x);}
        return log(1.0+exp(x));
    }
    fn tube_atan(x: F64) -> F64 {
        let ax: F64=abs(x);
        let mut z: F64=ax;
        let invert: Bool=ax>1.0;
        if invert {z=1.0/ax;}
        let a: F64=z/(1.0+sqrt(1.0+z*z));
        let square: F64=a*a;
        let mut power: F64=a;
        let mut sum: F64=0.0;
        for i in 0..26 {
            let v: F64=power/cast<F64>(2*i+1);
            if i%2==0 {sum=sum+v;}
            else {sum=sum-v;}
            power=power*square;
        }
        let mut result: F64=2.0*sum;
        if invert {result=0.5*pi()-result;}
        if x<0.0 {result= -result;}
        return result;
    }
    fn tube_plate_current(t: &TubeDef, vp0: F64, vg: F64) -> F64 {
        let vp: F64=min_f(vp0,0.0,1.0e9);
        if t.pentode {
            let e1: F64=t.screen/t.kp*tube_softplus(t.kp*(1.0/t.mu+vg/t.screen));
            return 2.0*exp(t.ex*log(min_f(e1,1.0e-200,1.0e200)))/t.kg1*tube_atan(vp/t.kvb);
        }
        let e1: F64=vp/t.kp*tube_softplus(t.kp*(1.0/t.mu+vg/sqrt(t.kvb+vp*vp)));
        if e1<=0.0 {return 0.0;}
        return 2.0*exp(t.ex*log(e1))/t.kg1;
    }
    fn tube_plate_voltage(t: &TubeDef, supply: F64, vg: F64) -> F64 {
        let mut lo: F64=0.0;
        let mut hi: F64=supply;
        for i in 0..36 {
            let mid: F64=(lo+hi)*0.5;
            if mid+t.load*tube_plate_current(t,mid,vg)>supply {hi=mid;}
            else {lo=mid;}
        }
        return (lo+hi)*0.5;
    }
    fn valve_blank() -> ValveChannel {return ValveChannel(0,0,0,0,1.0,1.0,0.0,0.0,0.0,0.0);}
    fn tube_set_rate(t: &mut Tube, sr: F64) -> Unit {
        let rate: F64=min_f(sr,16000.0,6144000.0);
        t.smoothing=1.0-exp(-1.0/(0.015*rate));
        t.envelopeAttack=1.0-exp(-1.0/(0.004*rate));
        t.envelopeRelease=1.0-exp(-1.0/(0.080*rate));
        t.transitionStep=1.0/(0.015*rate);
    }
    fn tube_create(sr: F64) -> Tube {
        let mut curves: Buffer<F32>=buffer<F32>(36882);
        for tube in 0..6 {
            let t: TubeDef=tube_definition(tube);
            for b in 0..3 {
                let mut gridScale: F64=0.55;
                if b==1 {gridScale=1.0;}
                if b==2 {gridScale=1.45;}
                let gridBias: F64=t.grid*gridScale;
                let supply: F64=t.plate+t.load*tube_plate_current(&t,t.plate,gridBias);
                let base: F64=tube_plate_voltage(&t,supply,gridBias);
                let probe: F64=0.0005*t.swing;
                let slope: F64=(tube_plate_voltage(&t,supply,gridBias-probe)-tube_plate_voltage(&t,supply,gridBias+probe))/(2.0*probe);
                let norm: F64=min_f(slope*t.swing,1.0e-6,1.0e300);
                for i in 0..2049 {
                    let normalized: F64=cast<F64>(i)*(5.0/2048.0)-2.5;
                    let plate: F64=tube_plate_voltage(&t,supply,gridBias+normalized*t.swing);
                    curves[(tube*3+b)*2049+i]=cast<F32>((base-plate)/norm);
                }
            }
        }
        let mut stage: Tube=Tube(move curves,array<Array<ValveChannel,2>>(valve_blank(),valve_blank()),
            0,0,1.0,0.0,0.0,1.0,0.001,0.001,0.0001,0.01);
        tube_set_rate(&mut stage,sr);
        return move stage;
    }
    fn tube_reset(t: &mut Tube) -> Unit {
        for c in 0..2 {
            t.channels[c]=ValveChannel(t.desiredType,t.desiredType,t.desiredTopology,t.desiredTopology,1.0,
                t.desiredDrive,t.desiredBias,t.desiredSag,0.0,0.0);
            if t.desiredType>0 {t.channels[c].wet=t.desiredWet;}
        }
    }
    fn tube_config(t: &mut Tube, typ: USize, topology: USize, driveDb: F64, bias: F64, sag: F64, wet: F64) -> Unit {
        t.desiredType=typ;
        if t.desiredType>6 {t.desiredType=6;}
        t.desiredTopology=topology;
        if t.desiredTopology>1 {t.desiredTopology=1;}
        t.desiredDrive=exp(log(10.0)*min_f(driveDb,0.0,36.0)/20.0);
        t.desiredBias=min_f(bias,-1.0,1.0);
        t.desiredSag=min_f(sag,0.0,1.0);
        t.desiredWet=min_f(wet,0.0,1.0);
        for i in 0..2 {
            if t.channels[i].newType!=t.desiredType || t.channels[i].newTopology!=t.desiredTopology {
                t.channels[i].oldType=t.channels[i].newType;
                t.channels[i].oldTopology=t.channels[i].newTopology;
                t.channels[i].newType=t.desiredType;
                t.channels[i].newTopology=t.desiredTopology;
                t.channels[i].transition=0.0;
            }
        }
    }
    fn tube_lookup(t: &Tube, typ: USize, x: F64, bias: F64) -> F64 {
        if typ==0 {return x;}
        let pos: F64=min_f(x,-2.5,2.5)*(2048.0/5.0)+1024.0;
        let index: USize=cast<USize>(trunc(min_f(pos,0.0,2047.0)));
        let fractional: F64=pos-cast<F64>(index);
        let biasPos: F64=min_f(bias,-1.0,1.0)+1.0;
        let biasIndex: USize=cast<USize>(trunc(min_f(biasPos,0.0,1.0)));
        let biasFraction: F64=biasPos-cast<F64>(biasIndex);
        let row: USize=(typ-1)*6147+biasIndex*2049+index;
        let row2: USize=row+2049;
        let u: F64=cast<F64>(t.curves[row])*(1.0-fractional)+cast<F64>(t.curves[row+1])*fractional;
        let v: F64=cast<F64>(t.curves[row2])*(1.0-fractional)+cast<F64>(t.curves[row2+1])*fractional;
        return u+(v-u)*biasFraction;
    }
    fn tube_evaluate(t: &Tube, typ: USize, topology: USize, x: F64, bias: F64) -> F64 {
        if typ==0 {return x;}
        if topology==0 {return tube_lookup(t,typ,x,bias);}
        return 0.5*(tube_lookup(t,typ,x,bias)-tube_lookup(t,typ,-x,bias));
    }
    fn tube_process(t: &mut Tube, x: F64, ch: USize) -> F64 {
        if !is_finite(x) {return 0.0;}
        if t.channels[ch].newType==0 && t.channels[ch].oldType==0 && t.channels[ch].wet<1.0e-10 {return x;}
        t.channels[ch].drive=t.channels[ch].drive+t.smoothing*(t.desiredDrive-t.channels[ch].drive);
        t.channels[ch].bias=t.channels[ch].bias+t.smoothing*(t.desiredBias-t.channels[ch].bias);
        t.channels[ch].sag=t.channels[ch].sag+t.smoothing*(t.desiredSag-t.channels[ch].sag);
        let mut targetWet: F64=0.0;
        if t.channels[ch].newType>0 {targetWet=t.desiredWet;}
        t.channels[ch].wet=t.channels[ch].wet+t.smoothing*(targetWet-t.channels[ch].wet);
        let level: F64=min_f(abs(x*t.channels[ch].drive),0.0,4.0);
        let mut alpha: F64=t.envelopeRelease;
        if level>t.channels[ch].envelope {alpha=t.envelopeAttack;}
        t.channels[ch].envelope=t.channels[ch].envelope+alpha*(level-t.channels[ch].envelope);
        let adjBias: F64=min_f(t.channels[ch].bias-0.27*t.channels[ch].sag*t.channels[ch].envelope,-1.0,1.0);
        let driven: F64=min_f(x*t.channels[ch].drive,-8.0,8.0);
        let newValue: F64=tube_evaluate(t,t.channels[ch].newType,t.channels[ch].newTopology,driven,adjBias);
        let mut processed: F64=newValue;
        if t.channels[ch].transition<1.0 {
            let oldValue: F64=tube_evaluate(t,t.channels[ch].oldType,t.channels[ch].oldTopology,driven,adjBias);
            processed=oldValue+t.channels[ch].transition*(newValue-oldValue);
            t.channels[ch].transition=min_f(t.channels[ch].transition+t.transitionStep,0.0,1.0);
            if t.channels[ch].transition==1.0 {
                t.channels[ch].oldType=t.channels[ch].newType;
                t.channels[ch].oldTopology=t.channels[ch].newTopology;
            }
        }
        let compensated: F64=processed/t.channels[ch].drive;
        let result: F64=x+t.channels[ch].wet*(compensated-x);
        if !is_finite(result) {return 0.0;}
        return min_f(result,-32.0,32.0);
    }

    fn band_blank_filter() -> Biquad {return Biquad(1.0,0.0,0.0,0.0,0.0,0.0,0.0);}
    fn band_blank_channel() -> BandChannel {
        return BandChannel(array<Array<Biquad,8>>(band_blank_filter(),band_blank_filter(),
            band_blank_filter(),band_blank_filter(),band_blank_filter(),band_blank_filter(),
            band_blank_filter(),band_blank_filter()),250.0,3000.0,I32(0));
    }
    fn band_coefficients(b: &mut Biquad, freq: F64, rate: F64, hp: Bool) -> Unit {
        let angle: F64=2.0*pi()*min_f(freq,10.0,0.45*rate)/rate;
        let c: F64=cos(angle);
        let sn: F64=sin(angle);
        let alpha: F64=sn/sqrt(2.0);
        let inv: F64=1.0/(1.0+alpha);
        if hp {
            b.b0=(1.0+c)*0.5*inv;
            b.b1=-(1.0+c)*inv;
        } else {
            b.b0=(1.0-c)*0.5*inv;
            b.b1=(1.0-c)*inv;
        }
        b.b2=b.b0;
        b.a1=-2.0*c*inv;
        b.a2=(1.0-alpha)*inv;
    }
    fn band_update_coeff(t: &mut ThreeBand, ch: USize) -> Unit {
        for i in 0..2 {
            band_coefficients(&mut t.channels[ch].filters[i],t.channels[ch].currentLower,t.rate,false);
            band_coefficients(&mut t.channels[ch].filters[i+2],t.channels[ch].currentLower,t.rate,true);
            band_coefficients(&mut t.channels[ch].filters[i+4],t.channels[ch].currentUpper,t.rate,false);
            band_coefficients(&mut t.channels[ch].filters[i+6],t.channels[ch].currentUpper,t.rate,true);
        }
    }
    fn band_reset(t: &mut ThreeBand) -> Unit {
        for i in 0..3 {tube_reset(&mut t.stages[i]);}
        for ch in 0..2 {
            t.releaseFrames[ch]=0;
            t.channels[ch].currentLower=t.targetLower;
            t.channels[ch].currentUpper=t.targetUpper;
            t.channels[ch].coefficientCounter=I32(0);
            for i in 0..8 {
                t.channels[ch].filters[i].z1=0.0;
                t.channels[ch].filters[i].z2=0.0;
            }
            band_update_coeff(t,ch);
        }
    }
    fn band_create(sr: F64) -> ThreeBand {
        let mut out: ThreeBand=ThreeBand(array<Array<Tube,3>>(tube_create(sr),tube_create(sr),tube_create(sr)),
            array<Array<BandChannel,2>>(band_blank_channel(),band_blank_channel()),
            88200.0,250.0,3000.0,1.0,false,zero<Array<USize,2>>(),zero<Array<USize,3>>(),zero<Array<F64,3>>());
        band_set_rate(&mut out,sr);
        band_reset(&mut out);
        return move out;
    }
    fn band_set_rate(t: &mut ThreeBand, sr: F64) -> Unit {
        let rate: F64=min_f(sr,16000.0,6144000.0);
        if rate==t.rate {return;}
        t.rate=rate;
        t.frequencySmoothing=1.0-exp(-64.0/(0.025*rate));
        for i in 0..3 {tube_set_rate(&mut t.stages[i],rate);}
        for ch in 0..2 {
            t.channels[ch].coefficientCounter=I32(0);
            band_update_coeff(t,ch);
        }
    }
    fn band_frequency(t: &mut ThreeBand, lowHz: F64, highHz: F64) -> Unit {
        let mut ceiling: F64=t.rate*0.45;
        if ceiling>12000.0 {ceiling=12000.0;}
        t.targetLower=min_f(lowHz,40.0,min_f(1400.0,0.0,ceiling*0.5));
        t.targetUpper=min_f(highHz,t.targetLower*1.5,ceiling);
    }
    fn band_update_enabled(t: &mut ThreeBand) -> Unit {
        let wasActive: Bool=t.anyActive;
        t.anyActive=false;
        for b in 0..3 {
            if t.types[b]>0 && t.wets[b]>0.0 {t.anyActive=true;}
        }
        if wasActive && !t.anyActive {
            for ch in 0..2 {t.releaseFrames[ch]=cast<USize>(trunc(0.10*t.rate));}
        }
    }
    fn band_config(t: &mut ThreeBand, band: USize, typ: USize, topology: USize,
        driveDb: F64, bias: F64, sag: F64, wet: F64) -> Unit {
        tube_config(&mut t.stages[band],typ,topology,driveDb,bias,sag,wet);
        t.types[band]=typ;
        t.wets[band]=min_f(wet,0.0,1.0);
        band_update_enabled(t);
    }
    fn band_process(t: &mut ThreeBand, x: F64, channel: USize) -> F64 {
        if !is_finite(x) {return 0.0;}
        t.channels[channel].coefficientCounter=t.channels[channel].coefficientCounter-I32(1);
        if t.channels[channel].coefficientCounter<I32(0) {
            t.channels[channel].coefficientCounter=I32(63);
            if t.channels[channel].currentLower!=t.targetLower || t.channels[channel].currentUpper!=t.targetUpper {
                t.channels[channel].currentLower=t.channels[channel].currentLower+
                    t.frequencySmoothing*(t.targetLower-t.channels[channel].currentLower);
                t.channels[channel].currentUpper=t.channels[channel].currentUpper+
                    t.frequencySmoothing*(t.targetUpper-t.channels[channel].currentUpper);
                if abs(t.channels[channel].currentLower-t.targetLower)<1.0e-7 {t.channels[channel].currentLower=t.targetLower;}
                if abs(t.channels[channel].currentUpper-t.targetUpper)<1.0e-7 {t.channels[channel].currentUpper=t.targetUpper;}
                band_update_coeff(t,channel);
            }
        }
        let mut low: F64=x;
        let mut rest: F64=x;
        for i in 0..2 {low=biquad_process(&mut t.channels[channel].filters[i],low);}
        for i in 2..4 {rest=biquad_process(&mut t.channels[channel].filters[i],rest);}
        let mut mid: F64=rest;
        let mut high: F64=rest;
        for i in 4..6 {mid=biquad_process(&mut t.channels[channel].filters[i],mid);}
        for i in 6..8 {high=biquad_process(&mut t.channels[channel].filters[i],high);}
        if !t.anyActive && t.releaseFrames[channel]==0 {return x;}
        if !t.anyActive {t.releaseFrames[channel]=t.releaseFrames[channel]-1;}
        let lowWet: F64=tube_process(&mut t.stages[0],low,channel);
        let midWet: F64=tube_process(&mut t.stages[1],mid,channel);
        let highWet: F64=tube_process(&mut t.stages[2],high,channel);
        let y: F64=x+(lowWet-low)+(midWet-mid)+(highWet-high);
        if !is_finite(y) {return 0.0;}
        return min_f(y,-32.0,32.0);
    }
    fn apply_tube(e: &mut Engine, signal: F64, channel: USize) -> F64 {
        let weight: F64=e.tubeRouteBlend[channel];
        let target: F64=cast<F64>(e.tubeRouting);
        if weight<target {e.tubeRouteBlend[channel]=min_f(weight+e.tubeRouteStep,0.0,target);}
        else if weight>target {e.tubeRouteBlend[channel]=min_f(weight-e.tubeRouteStep,target,1.0);}
        if e.tubeRouteBlend[channel]<=0.0 {return tube_process(&mut e.tube,signal,channel);}
        if e.tubeRouteBlend[channel]>=1.0 {return band_process(&mut e.threeBand,signal,channel);}
        let one: F64=tube_process(&mut e.tube,signal,channel);
        let three: F64=band_process(&mut e.threeBand,signal,channel);
        return one+e.tubeRouteBlend[channel]*(three-one);
    }

    fn output_prepare(rate: F64) -> OutputStage {
        let sr: F64 = min_f(rate,8000.0,384000.0);
        let mut o: OutputStage = OutputStage(zero<Array<F64,256>>(),zero<Array<F64,128>>(),
            zero<Array<F64,68>>(),0,false,0.0,0.0,1.0,1.0,
            1.0-exp(-1.0/(0.50*sr)),1.0-exp(-1.0/(0.25*sr)),1.0-exp(-1.0/(0.12*sr)),
            0.0,0.0,0.0);
        for phase in 0..4 {
            let mut sum: F64=0.0;
            for tap in 0..17 {
                let j: F64 = cast<F64>(tap)-8.0;
                let x: F64=j-cast<F64>(phase)*0.25;
                let w: F64=0.42+0.5*cos(pi()*j/8.0)+0.08*cos(2.0*pi()*j/8.0);
                let mut sinc: F64=1.0;
                if abs(x)>=1.0e-12 {sinc=sin(pi()*x)/(pi()*x);}
                let c: F64=sinc*w;
                o.kernels[phase*17+tap]=c;
                sum=sum+c;
            }
            for tap in 0..17 {o.kernels[phase*17+tap]=o.kernels[phase*17+tap]/sum;}
        }
        return o;
    }
    fn output_reset(o: &mut OutputStage) -> Unit {
        for i in 0..256 {o.signal[i]=0.0;}
        for i in 0..128 {o.peakHistory[i]=0.0;}
        o.index=0;
        o.limiterPreviouslyEnabled=false;
        o.inputPower=0.0;o.processedPower=0.0;
        o.autoGain=1.0;o.limiterGain=1.0;
        o.estimatedPeak=0.0;o.reductionDb=0.0;o.autoGainDb=0.0;
    }
    fn output_estimate(o: &OutputStage, center: USize) -> F64 {
        let mut peak: F64=0.0;
        for phase in 0..4 {
            for channel in 0..2 {
                let mut sample: F64=0.0;
                for tap in 0..17 {
                    let position: USize=(center+tap+120)%128;
                    sample=sample+o.signal[channel*128+position]*o.kernels[phase*17+tap];
                }
                if abs(sample)>peak {peak=abs(sample);}
            }
        }
        return peak;
    }
    unsafe fn output_process(o: &mut OutputStage, left: Ptr<F32>, right: Ptr<F32>, frames: USize,
                             inputMeanSquare: F64, autoMatch: Bool, limiter: Bool, ceilingDb: F64) -> Unit
                             guarantees(NoAlloc,NoBlocking,NoIO) {
        if frames==0 {return;}
        let mut lb: ForeignBuffer<F32>=foreign(left,frames);
        let mut rb: ForeignBuffer<F32>=foreign(right,frames);
        let power: F64=min_f(inputMeanSquare,0.0,1024.0);
        let ceiling: F64=exp(log(10.0)*min_f(ceilingDb,-12.0,-0.1)/20.0);
        let wasLimiter: Bool=o.limiterPreviouslyEnabled;
        o.limiterPreviouslyEnabled=limiter;
        let mut blockPeak: F64=0.0;
        let mut blockReduction: F64=0.0;
        for i in 0..frames {
            let mut l: F64=cast<F64>(lb[i]);
            let mut r: F64=cast<F64>(rb[i]);
            if !is_finite(l) {l=0.0;}
            if !is_finite(r) {r=0.0;}
            o.inputPower=o.inputPower+o.powerAlpha*(power-o.inputPower);
            o.processedPower=o.processedPower+o.powerAlpha*(0.5*(l*l+r*r)-o.processedPower);
            let mut target: F64=1.0;
            if autoMatch && o.inputPower>1.0e-8 && o.processedPower>1.0e-8 {
                target=min_f(sqrt(o.inputPower/o.processedPower),0.125,2.0);
            }
            o.autoGain=o.autoGain+o.matchAlpha*(target-o.autoGain);
            o.autoGainDb=20.0*log(min_f(o.autoGain,1.0e-8,1.0e12))/log(10.0);
            let ring: USize=o.index%128;
            o.signal[ring]=l*o.autoGain;
            o.signal[128+ring]=r*o.autoGain;
            if limiter && o.index>=8 {
                let center: USize=o.index-8;
                if !wasLimiter && i==0 && o.index>=16 {
                    for k in o.index-16..center {o.peakHistory[k%128]=output_estimate(o,k);}
                }
                o.peakHistory[center%128]=output_estimate(o,center);
            }
            if o.index<16 {
                lb[i]=F32(0.0);rb[i]=F32(0.0);
                o.index=o.index+1;
                continue;
            }
            let outIndex: USize=o.index-16;
            let mut futurePeak: F64=0.0;
            if limiter {
                for k in outIndex..o.index-7 {
                    if o.peakHistory[k%128]>futurePeak {futurePeak=o.peakHistory[k%128];}
                }
            }
            if futurePeak>blockPeak {blockPeak=futurePeak;}
            let mut requested: F64=1.0;
            if limiter && futurePeak>0.0 {requested=min_f(ceiling/(futurePeak*1.005),0.0,1.0);}
            if requested<o.limiterGain {o.limiterGain=requested;}
            else {o.limiterGain=o.limiterGain+o.releaseAlpha*(requested-o.limiterGain);}
            let mut outL: F64=o.signal[outIndex%128]*o.limiterGain;
            let mut outR: F64=o.signal[128+outIndex%128]*o.limiterGain;
            if limiter {
                outL=min_f(outL,-ceiling,ceiling);
                outR=min_f(outR,-ceiling,ceiling);
            }
            lb[i]=cast<F32>(outL);
            rb[i]=cast<F32>(outR);
            let reduction: F64=20.0*log(min_f(o.limiterGain,1.0e-8,1.0e12))/log(10.0);
            if reduction<blockReduction {blockReduction=reduction;}
            o.index=o.index+1;
        }
        o.estimatedPeak=blockPeak;
        o.reductionDb=blockReduction;
    }

    fn fir_create(n: USize, upsampler: Bool) -> Fir {
        let mut taps: Buffer<F64> = buffer<F64>(n);
        let history: Buffer<F64> = buffer<F64>(2 * n);
        let middle: F64 = (cast<F64>(n) - 1.0) * 0.5;
        let mut total: F64 = 0.0;
        for i in 0..n {
            let x: F64 = 2.0 * 0.245 * (cast<F64>(i) - middle);
            let mut si: F64 = 1.0;
            if abs(x) >= 1.0e-12 { si = sin(pi() * x) / (pi() * x); }
            let a: F64 = 2.0 * pi() * cast<F64>(i) / cast<F64>(n - 1);
            let window: F64 = 0.35875 - 0.48829 * cos(a) + 0.14128 * cos(2.0 * a) - 0.01168 * cos(3.0 * a);
            let v: F64 = (2.0 * 0.245) * si * window;
            taps[i] = v;
            total = total + v;
        }
        let mut factor: F64 = 1.0 / total;
        if upsampler { factor = 2.0 / total; }
        for i in 0..n { taps[i] = taps[i] * factor; }
        return Fir(move taps, move history, 0, n);
    }

    fn fir_reset(f: &mut Fir) -> Unit {
        for i in 0..len(&f.history) { f.history[i] = 0.0; }
        f.index = 0;
    }
    fn fir_push(f: &mut Fir, x: F64) -> Unit {
        let i: USize = f.index;
        f.history[i] = x;
        f.history[i + f.size] = x;
        f.index = i + 1;
        if f.index == f.size { f.index = 0; }
    }
    fn fir_upsample(f: &mut Fir, x: F64) -> Stereo {
        fir_push(f, x);
        let newest: USize = f.index + f.size - 1;
        let ec: USize = (f.size + 1) / 2;
        let oc: USize = f.size / 2;
        let mut even: F64 = 0.0;
        let mut odd: F64 = 0.0;
        for k in 0..ec / 2 {
            even = even + f.taps[2 * k] * (f.history[newest - k] + f.history[newest - (ec - 1 - k)]);
        }
        if ec % 2 != 0 { even = even + f.taps[2 * (ec / 2)] * f.history[newest - ec / 2]; }
        for k in 0..oc / 2 {
            odd = odd + f.taps[2 * k + 1] * (f.history[newest - k] + f.history[newest - (oc - 1 - k)]);
        }
        if oc % 2 != 0 { odd = odd + f.taps[2 * (oc / 2) + 1] * f.history[newest - oc / 2]; }
        return Stereo(even, odd);
    }
    fn fir_downsample(f: &mut Fir, a: F64, b: F64) -> F64 {
        fir_push(f, a);
        fir_push(f, b);
        let newest: USize = f.index + f.size - 1;
        let half: USize = f.size / 2;
        let mut s: F64 = f.taps[half] * f.history[newest - half];
        for k in 0..half {
            s = s + f.taps[k] * (f.history[newest - k] + f.history[newest - (f.size - 1 - k)]);
        }
        return s;
    }
    fn folded_compensation(depth: USize) -> USize {
        if depth==0 {return 62;}
        if depth==1 {return 31;}
        if depth==2 {return 16;}
        if depth==3 {return 8;}
        if depth==4 {return 4;}
        return 2;
    }
    fn folded_reset(f: &mut Folded) -> Unit {
        for i in 0..1536 {f.history[i]=0.0;}
        f.index=0;
    }
    fn folded_prepare() -> Folded {
        let mut f: Folded=Folded(zero<Array<F64,2304>>(),zero<Array<USize,6>>(),
            zero<Array<F64,1536>>(),0);
        for depth in 0..6 {
            let mut ups: Buffer<Fir>=buffer<Fir>(0);
            let mut downs: Buffer<Fir>=buffer<Fir>(0);
            push(&mut ups,fir_create(191,true));
            push(&mut downs,fir_create(191,false));
            for st in 0..5 {
                push(&mut ups,fir_create(127,true));
                push(&mut downs,fir_create(127,false));
            }
            let mut impulse: Array<F64,384>=zero<Array<F64,384>>();
            for n in 0..384 {
                let mut high: Array<F64,64>=zero<Array<F64,64>>();
                let mut value: F64=0.0;
                if n==0 {value=1.0;}
                let first: Stereo=fir_upsample(&mut ups[0],value);
                high[0]=first.left;high[1]=first.right;
                let mut count: USize=2;
                for stage in 0..depth {
                    let mut next: Array<F64,64>=zero<Array<F64,64>>();
                    for i in 0..count {
                        let a: Stereo=fir_upsample(&mut ups[stage+1],high[i]);
                        next[2*i]=a.left;
                        next[2*i+1]=a.right;
                    }
                    for i in 0..count*2 {high[i]=next[i];}
                    count=count*2;
                }
                let mut stage: USize=depth;
                while stage>0 {
                    stage=stage-1;
                    for i in 0..count/2 {
                        high[i]=fir_downsample(&mut downs[stage+1],high[2*i],high[2*i+1]);
                    }
                    count=count/2;
                }
                let output: F64=fir_downsample(&mut downs[0],high[0],high[1]);
                let index: USize=n+folded_compensation(depth);
                if index<384 {impulse[index]=output;}
            }
            let mut used: USize=384;
            while used>1 && abs(impulse[used-1])<1.0e-17 {used=used-1;}
            f.lengths[depth]=used;
            for k in 0..used {f.coefficients[depth*384+k]=impulse[used-1-k];}
        }
        return f;
    }
    fn folded_push(f: &mut Folded, l: F64, r: F64) -> Unit {
        f.history[f.index]=l;
        f.history[f.index+384]=l;
        f.history[f.index+768]=r;
        f.history[f.index+1152]=r;
        f.index=f.index+1;
        if f.index==384 {f.index=0;}
    }
    fn folded_eval(f: &Folded, ch: USize, depth: USize) -> F64 {
        let n: USize=f.lengths[depth];
        let start: USize=ch*768+f.index+384-n;
        let mut result: F64=0.0;
        for k in 0..n {
            result=result+f.history[start+k]*f.coefficients[depth*384+k];
        }
        return result;
    }

    fn if_model(dac: USize,pulse: USize) -> USize {
        if dac==0 {return pulse;}
        if dac==3 {return 2;}
        return 0;
    }
    fn channel_sample(e: &mut Engine, x: F64, ch: USize) -> F64 {
        let fastTubes: Bool=e.tubeCpuMode==1 && e.depth>=4;
        let mut high: Array<F64,64> = zero<Array<F64,64>>();
        let first: Stereo = fir_upsample(&mut e.up[ch], x);
        high[0] = first.left;
        high[1] = first.right;
        let mut count: USize = 2;
        for stage in 0..e.depth {
            let mut next: Array<F64,64> = zero<Array<F64,64>>();
            for i in 0..count {
                let result: Stereo = fir_upsample(&mut e.up[2 + stage * 2 + ch], high[i]);
                next[i * 2] = result.left;
                next[i * 2 + 1] = result.right;
            }
            for i in 0..count * 2 { high[i] = next[i]; }
            count = count * 2;
            if fastTubes && count==16 && e.tubeActive {
                for i in 0..count {high[i]=apply_tube(e,high[i],ch);}
            }
        }
        for i in 0..count {
            let mut tubeInput: F64=high[i];
            if e.tubeActive && !fastTubes {tubeInput=apply_tube(e,tubeInput,ch);}
            let colored: F64=pulse_color(color_sample(tubeInput,e.characterCurrent),e.pulseColorCurrent,
                if_model(e.dacMode,e.pulseMode));
            if e.dacMode==0 {high[i]=pulse_process(&mut e.pulses[ch],colored);}
            else {high[i]=dac_process(&mut e.dacs[ch],colored,e.mismatch);}
        }
        let mut stage: USize = e.depth;
        while stage > 0 {
            stage = stage - 1;
            for i in 0..count / 2 {
                high[i] = fir_downsample(&mut e.down[2 + stage * 2 + ch], high[2 * i], high[2 * i + 1]);
            }
            count = count / 2;
        }
        return fir_downsample(&mut e.down[ch], high[0], high[1]);
    }

    fn biquad_create(sr: F64) -> Biquad {
        if sr < 88000.0 { return Biquad(1.0,0.0,0.0,0.0,0.0,0.0,0.0); }
        let w: F64 = 2.0 * pi() * 40000.0 / sr;
        let cs: F64 = cos(w);
        let alpha: F64 = sin(w) / sqrt(2.0);
        let a0: F64 = 1.0 + alpha;
        return Biquad(((1.0 - cs) * 0.5) / a0, (1.0-cs)/a0,
                       ((1.0-cs)*0.5)/a0,(-2.0*cs)/a0,(1.0-alpha)/a0,0.0,0.0);
    }
    fn biquad_process(b: &mut Biquad, x: F64) -> F64 {
        let y: F64 = b.b0 * x + b.z1;
        b.z1 = b.b1 * x - b.a1 * y + b.z2;
        b.z2 = b.b2 * x - b.a2 * y;
        return y;
    }
    fn allpass_create() -> Allpass { return Allpass(0.0,0.0); }
    fn ap_coeff(hz: F64, rate: F64) -> F64 {
        let angle: F64 = pi() * min_f(hz,0.0,rate*0.35) / rate;
        let tangent: F64 = sin(angle) / cos(angle);
        return (tangent - 1.0) / (tangent + 1.0);
    }
    fn creative_create(sr: F64) -> Creative {
        let rate: F64 = min_f(sr,8000.0,384000.0);
        return Creative(zero<Array<F64,3>>(),zero<Array<F64,2>>(),zero<Array<F64,2>>(),zero<Array<F64,2>>(),
            array<Array<Allpass,4>>(allpass_create(),allpass_create(),allpass_create(),allpass_create()),
            1.0-exp(-1.0/(0.020*rate)), 1.0-exp(-2.0*pi()*1750.0/rate),
            1.0-exp(-2.0*pi()*3200.0/rate), 1.0-exp(-2.0*pi()*7200.0/rate),
            ap_coeff(900.0,rate),ap_coeff(3200.0,rate));
    }
    fn allpass_process(s: &mut Allpass,x: F64,a: F64) -> F64 {
        let y: F64 = a*x + s.input-a*s.output;
        s.input=x;
        s.output=y;
        return y;
    }
    fn creative_channel(c: &mut Creative,x: F64,ch: USize,phaseMode: U32,filterMode: U32) -> F64 {
        c.focusState[ch] = c.focusState[ch]+c.focusPole*(x-c.focusState[ch]);
        c.warmState[ch] = c.warmState[ch]+c.warmPole*(x-c.warmState[ch]);
        c.hfState[ch] = c.hfState[ch]+c.hfPole*(x-c.hfState[ch]);
        let mut y: F64 = x+(0.95*c.amounts[0])*(x-c.focusState[ch]);
        let ap1: F64 = allpass_process(&mut c.allpass[ch],y,c.ap0);
        let ap2: F64 = allpass_process(&mut c.allpass[2+ch],ap1,c.ap1);
        if phaseMode == U32(1) { y = y+(0.43*c.amounts[1])*(ap2-y); }
        else if phaseMode == U32(2) { y = y+(0.30*c.amounts[1])*(ap1-y); }
        if filterMode != U32(0) {
            let mut low: F64 = c.hfState[ch];
            if filterMode >= U32(2) {low = c.warmState[ch];}
            let mut depth: F64 = 0.72;
            if filterMode == U32(2) {depth=0.78;}
            if filterMode == U32(3) {depth=0.90;}
            y = y+(depth*c.amounts[2])*(low-x);
        }
        return y;
    }
    fn round_away(x: F64) -> F64 {
        if x >= 0.0 {return floor(x+0.5);}
        return ceil(x-0.5);
    }
    fn next_rand(state: &mut U32) -> F64 {
        let mut r: U32 = *state;
        r = r ^ (r << 13);
        r = r ^ (r >> 17);
        r = r ^ (r << 5);
        *state = r;
        return cast<F64>(r) * (1.0/4294967296.0);
    }
    fn pulse_create(mode: USize,channel: USize) -> Pulse {
        let mut p: Pulse = Pulse(zero<Array<F64,12>>(),zero<Array<F64,11>>(),zero<Array<F64,110>>(),zero<Array<F64,100>>(),
            0,0,0,U32(0),U32(0),0.0);
        if mode == 1 {p.order=5;p.elements=4;}
        if mode == 2 {p.order=11;p.elements=10;}
        p.ntf[0]=1.0;
        for i in 0..p.order {
            let mut k: USize = i+1;
            while k > 0 {
                p.ntf[k] = p.ntf[k]-0.30*p.ntf[k-1];
                k = k-1;
            }
        }
        p.seed = wrapping_add(U32(0x9E3779B9),wrapping_mul(cast<U32>(channel),U32(0x85EBCA6B)));
        p.rng = p.seed;
        if p.elements != 0 {
            for origin in 0..p.elements {
                p.prefixes[origin*11]=0.0;
                for i in 0..p.elements {
                    let cell: USize = (origin+i)%p.elements;
                    let centered: F64 = (2.0*cast<F64>(cell)-cast<F64>(p.elements)+1.0)/cast<F64>(p.elements);
                    let gain: F64 = 1.0+0.000001*centered;
                    p.gains[origin*10+i]=gain;
                    p.prefixes[origin*11+i+1]=p.prefixes[origin*11+i]+gain;
                }
            }
            p.totalGain=p.prefixes[p.elements];
        }
        return p;
    }
    fn pulse_reset(p: &mut Pulse) -> Unit {
        for i in 0..11 {p.errors[i]=0.0;}
        p.position=0;p.rng=p.seed;
    }
    fn pulse_process(p: &mut Pulse, signal: F64) -> F64 {
        if p.order == 0 || !is_finite(signal) {return signal;}
        if abs(signal)>7.5 {pulse_reset(p);return signal;}
        let mut feedback: F64 = 0.0;
        for k in 1..p.order+1 {feedback=feedback-p.ntf[k]*p.errors[k-1];}
        let input: F64 = signal+feedback;
        let step: F64 = 1.0/1048576.0;
        let tpdf: F64 = (next_rand(&mut p.rng)-next_rand(&mut p.rng))*step;
        let q: F64 = round_away((input+tpdf)/step)*step;
        let err: F64 = q-input;
        let mut k: USize = p.order;
        while k > 1 { k=k-1;p.errors[k]=p.errors[k-1]; }
        p.errors[0]=err;
        if q <= -1.0 || q >= 1.0 {return q;}
        let duty: F64 = (q+1.0)*(0.5*cast<F64>(p.elements));
        let full: USize = cast<USize>(trunc(duty));
        let fraction: F64 = duty-cast<F64>(full);
        let base: USize = p.position;
        let sum: F64 = p.prefixes[base*11+full]+fraction*p.gains[base*10+full];
        p.position=(p.position+1)%p.elements;
        return 2.0*sum/p.totalGain-1.0;
    }
    fn dac_create(mode: USize,channel: USize) -> Dac {
        let mut d: Dac = Dac(zero<Array<F64,12>>(),zero<Array<F64,11>>(),zero<Array<F64,110>>(),zero<Array<F64,100>>(),
            0,mode,0,U32(0),U32(0));
        if mode == 2 {d.order=5;}
        if mode == 3 {d.order=11;}
        d.seed = U32(0xA341316C) ^ wrapping_mul(U32(0x9E3779B9),wrapping_add(cast<U32>(channel),U32(1)));
        d.rng=d.seed;
        d.ntf[0]=1.0;
        let mut a: F64=0.30;
        if mode == 2 {a=0.48;}
        for i in 0..d.order {
            let mut k: USize=i+1;
            while k>0 {d.ntf[k]=d.ntf[k]-a*d.ntf[k-1];k=k-1;}
        }
        for origin in 0..10 {
            d.prefixes[origin*11]=0.0;
            for i in 0..10 {
                let cell: USize=(origin+i)%10;
                let centered: F64=(2.0*cast<F64>(cell)+1.0-10.0)/10.0;
                d.gains[origin*10+i]=centered;
                d.prefixes[origin*11+i+1]=d.prefixes[origin*11+i]+centered;
            }
        }
        return d;
    }
    fn dac_reset(d: &mut Dac) -> Unit {
        for i in 0..11 {d.errors[i]=0.0;}
        d.rotation=0;d.rng=d.seed;
    }
    fn dac_shaped(d: &mut Dac,signal: F64,quantum: F64) -> F64 {
        let mut correction: F64=0.0;
        for k in 1..d.order+1 {correction=correction-d.ntf[k]*d.errors[k-1];}
        let input: F64=signal+correction;
        let dither: F64=(next_rand(&mut d.rng)-next_rand(&mut d.rng))*quantum;
        let q: F64=round_away((input+dither)/quantum)*quantum-dither;
        let err: F64=q-input;
        let mut k: USize=d.order;
        while k>1 {k=k-1;d.errors[k]=d.errors[k-1];}
        if d.order>0 {d.errors[0]=err;}
        return q;
    }
    fn dac_process(d: &mut Dac,signal: F64,amount: F64) -> F64 {
        if !is_finite(signal) {dac_reset(d);return 0.0;}
        if d.mode==0 {return signal;}
        if abs(signal)>4.0 {dac_reset(d);return signal;}
        if d.mode==1 {
            let quantum: F64=1.0/262144.0;
            let dither: F64=(next_rand(&mut d.rng)-next_rand(&mut d.rng))*quantum;
            let q: F64=round_away((signal+dither)/quantum)*quantum-dither;
            return q+amount*2.0e-5*q*(q*q-0.25);
        }
        let q: F64=dac_shaped(d,signal,1.0/1048576.0);
        if d.mode==2 || q<=-1.0 || q>=1.0 {return q;}
        let duty: F64=(q+1.0)*5.0;
        let mut full: USize=cast<USize>(trunc(duty));
        if full>9 {full=9;}
        let fraction: F64=duty-cast<F64>(full);
        let origin: USize=d.rotation;
        let gainError: F64=d.prefixes[origin*11+full]+fraction*d.gains[origin*10+full];
        d.rotation=(d.rotation+1)%10;
        return q+amount*2.0e-5*(2.0/10.0)*gainError;
    }
    fn color_sample(x: F64,amount: F64) -> F64 {
        let drive: F64=0.6*amount;
        if drive<=1.0e-8 {return x;}
        let z: F64=drive*x;
        if abs(z)>2.0 {
            if z>20.0 {return 1.0/drive;}
            if z< -20.0 {return -1.0/drive;}
            return (exp(2.0*z)-1.0)/(exp(2.0*z)+1.0)/drive;
        }
        let z2: F64=z*z;
        let num: F64=135135.0+z2*(17325.0+z2*(378.0+z2));
        let den: F64=135135.0+z2*(62370.0+z2*(3150.0+28.0*z2));
        return z*num/(den*drive);
    }
    fn pulse_color(x: F64,strength: F64,model: USize) -> F64 {
        if model==0 || strength<=0.0 {return x;}
        let s: F64=min_f(strength,0.0,1.0);
        let bounded: F64=min_f(x,-2.0,2.0);
        let sq: F64=bounded*bounded;
        let mut extra: F64=0.08*sq-0.30*sq*bounded;
        if model==1 {extra=0.21*sq-0.15*sq*bounded;}
        return x+s*extra;
    }
    fn create_engine(sr: F64, depth: USize, pulseMode: USize, dacMode: USize, mismatch: F64) -> Engine {
        let rate: F64 = min_f(sr, 8000.0, 384000.0);
        let mut ups: Buffer<Fir> = buffer<Fir>(0);
        let mut downs: Buffer<Fir> = buffer<Fir>(0);
        for c in 0..2 { push(&mut ups, fir_create(191,true));push(&mut downs,fir_create(191,false)); }
        for s in 0..5 {
            for c in 0..2 { push(&mut ups, fir_create(127,true));push(&mut downs,fir_create(127,false)); }
        }
        let mut samples: USize = cast<USize>(floor(rate * 0.00015 + 0.5));
        if samples < 1 { samples = 1; }
        if samples > 63 { samples = 63; }
        let smooth: F64 = 1.0-exp(-1.0 / (0.020 * rate));
        let cross: F64 = 1.0-exp(-2.0*pi()*700.0 / rate);
        let warm: F64 = 1.0-exp(-2.0*pi()*7500.0 / rate);
        return Engine(move ups,move downs,
            zero<Array<F64,157>>(),zero<Array<F64,157>>(),
            zero<Array<F64,63>>(),zero<Array<F64,63>>(),
            zero<Array<F64,64>>(),zero<Array<F64,64>>(),
            0,0,0,0.0,0.0,cross,samples,
            0.0,1.0,1.0,smooth,
            0.0,0.0,warm,0.0,0.0,rate>=88000.0,
            biquad_create(rate),biquad_create(rate),creative_create(rate),
            array<Array<Pulse,2>>(pulse_create(pulseMode,0),pulse_create(pulseMode,1)),
            array<Array<Dac,2>>(dac_create(dacMode,0),dac_create(dacMode,1)),
            pulseMode,dacMode,mismatch,0.0,0.0,depth,rate,output_prepare(rate),
            tube_create(rate*cast<F64>(2 << depth)),false,
            band_create(rate*cast<F64>(2 << depth)),0,zero<Array<F64,2>>(),
            1.0/(0.025*rate*cast<F64>(2 << depth)),
            depth,pulseMode,dacMode,0,false,
            folded_prepare(),false,false,0,0,true,0.0,depth,depth,128,0);
    }
    export fn live_latency_final() -> U32 {return U32(173);}
    export fn live_latency() -> U32 guarantees(NoAlloc,NoBlocking,NoIO,NoUnwind,BoundedWork,ThreadSafe) {return U32(157);}
    export fn live_create(rate: F64, depth: U32) -> OpaqueHandle {
        if !is_finite(rate) || rate <= 0.0 || depth > U32(5) { return handle(null<U8>()); }
        return live_create_ex(rate,depth,U32(0),U32(0),F32(0.25));
    }
    export fn live_create_ex(rate: F64, depth: U32, pulseMode: U32, dacMode: U32, mismatch: F32) -> OpaqueHandle {
        if !is_finite(rate) || rate<=0.0 || depth>U32(5) || pulseMode>U32(2) || dacMode>U32(3) || !is_finite(cast<F64>(mismatch)) {return handle(null<U8>());}
        let owner: Box<Engine> = box<Engine>(create_engine(rate,cast<USize>(depth),cast<USize>(pulseMode),cast<USize>(dacMode),min_f(cast<F64>(mismatch),0.0,1.0)));
        return handle(into_raw(move owner));
    }
    export unsafe fn live_destroy(h: OpaqueHandle) -> Unit {
        let owner: Box<Engine> = from_raw(handle_ptr<Engine>(h));
    }
    export unsafe fn live_reset(h: OpaqueHandle) -> Unit {
        let e: Ptr<Engine> = handle_ptr<Engine>(h);
        for i in 0..len(&(*e).up) { fir_reset(&mut (*e).up[i]); fir_reset(&mut (*e).down[i]); }
        for i in 0..157 { (*e).dryL[i] = 0.0; (*e).dryR[i] = 0.0; }
        for i in 0..63 { (*e).alignedL[i] = 0.0; (*e).alignedR[i] = 0.0; }
        for i in 0..64 { (*e).delayL[i] = 0.0; (*e).delayR[i] = 0.0; }
        (*e).dryIndex = 0;
        (*e).alignIndex = 0;
        (*e).crossIndex = 0;
        (*e).crossLowL = 0.0;
        (*e).crossLowR = 0.0;
        (*e).warmLowL = 0.0;
        (*e).warmLowR = 0.0;
        (*e).warmMix = 0.0;
        (*e).hfMix = 0.0;
        (*e).hfL.z1 = 0.0; (*e).hfL.z2 = 0.0;
        (*e).hfR.z1 = 0.0; (*e).hfR.z2 = 0.0;
        for i in 0..3 { (*e).creative.amounts[i] = 0.0; }
        for i in 0..2 {
            (*e).creative.focusState[i]=0.0;
            (*e).creative.warmState[i]=0.0;
            (*e).creative.hfState[i]=0.0;
        }
        for i in 0..4 {
            (*e).creative.allpass[i].input=0.0;
            (*e).creative.allpass[i].output=0.0;
        }
        pulse_reset(&mut (*e).pulses[0]);pulse_reset(&mut (*e).pulses[1]);
        dac_reset(&mut (*e).dacs[0]);dac_reset(&mut (*e).dacs[1]);
        output_reset(&mut (*e).output);
        tube_reset(&mut (*e).tube);
        band_reset(&mut (*e).threeBand);
        (*e).tubeRouteBlend[0]=cast<F64>((*e).tubeRouting);
        (*e).tubeRouteBlend[1]=cast<F64>((*e).tubeRouting);
        (*e).pendingDepth=(*e).depth;
        (*e).pendingPulse=(*e).pulseMode;
        (*e).pendingDac=(*e).dacMode;
        (*e).transitionCounter=0;(*e).transitioning=false;
        folded_reset(&mut (*e).folded);
        (*e).foldedHistoryActive=false;
        (*e).foldedPriming=0;
        (*e).legacyRunning=true;
        (*e).legacyPriming=0;
        (*e).fastWeight=0.0;
    }
    export unsafe fn live_process(h: OpaqueHandle, left: Ptr<F32>, right: Ptr<F32>, frames: USize,
                                   crossfeed: F32, mix: F32, gainDb: F32, filterMode: U32) -> U32 {
        return live_process_ex(h,left,right,frames,crossfeed,mix,gainDb,filterMode,0,0.0,0.0,0.0);
    }
    export unsafe fn live_process_ex(h: OpaqueHandle, left: Ptr<F32>, right: Ptr<F32>, frames: USize,
                                   crossfeed: F32, mix: F32, gainDb: F32, filterMode: U32,
                                   phaseMode: U32, focus: F32, phase: F32, tone: F32) -> U32 {
        return live_process_full(h,left,right,frames,crossfeed,mix,gainDb,filterMode,phaseMode,focus,phase,tone,0.0,0.0);
    }
    export unsafe fn live_tube_cpu_mode(h: OpaqueHandle, mode: U32) -> U32 {
        if mode>U32(1) {return U32(0);}
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        (*e).tubeCpuMode=cast<USize>(mode);
        let mut depth: USize=(*e).depth;
        if (*e).tubeCpuMode==1 && depth>=4 {depth=3;}
        let rate: F64=(*e).sampleRate*cast<F64>(2 << depth);
        tube_set_rate(&mut (*e).tube,rate);
        band_set_rate(&mut (*e).threeBand,rate);
        (*e).tubeRouteStep=1.0/(0.025*rate);
        return U32(1);
    }
    export unsafe fn live_fast_mode(h: OpaqueHandle, enabled: Bool) -> U32 {
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        (*e).fastEnabled=enabled;
        return U32(1);
    }
    export unsafe fn live_request_mode(h: OpaqueHandle, depth: U32, pulseMode: U32, dacMode: U32, mismatch: F32) -> U32 {
        if depth>U32(5) || pulseMode>U32(2) || dacMode>U32(3) || !is_finite(cast<F64>(mismatch)) {return U32(0);}
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        (*e).mismatch=min_f(cast<F64>(mismatch),0.0,1.0);
        if !(*e).transitioning && (cast<USize>(depth)!=(*e).depth ||
            cast<USize>(pulseMode)!=(*e).pulseMode || cast<USize>(dacMode)!=(*e).dacMode) {
            (*e).pendingDepth=cast<USize>(depth);
            (*e).pendingPulse=cast<USize>(pulseMode);
            (*e).pendingDac=cast<USize>(dacMode);
            (*e).transitionCounter=0;
            (*e).transitioning=true;
        }
        return U32(1);
    }
    export unsafe fn live_process_full(h: OpaqueHandle, left: Ptr<F32>, right: Ptr<F32>, frames: USize,
                                   crossfeed: F32, mix: F32, gainDb: F32, filterMode: U32,
                                   phaseMode: U32, focus: F32, phase: F32, tone: F32,
                                   character: F32, pulseIntensity: F32) -> U32 guarantees(NoAlloc, NoBlocking, NoIO) {
        let e: Ptr<Engine> = handle_ptr<Engine>(h);
        let mut lbuf: ForeignBuffer<F32> = foreign(left, frames);
        let mut rbuf: ForeignBuffer<F32> = foreign(right, frames);
        let xTarget: F64 = min_f(cast<F64>(crossfeed),0.0,1.0);
        let mTarget: F64 = min_f(cast<F64>(mix),0.0,1.0);
        let gainTarget: F64 = exp(log(10.0)*min_f(cast<F64>(gainDb),-24.0,12.0)/20.0);
        let mode: U32 = filterMode;
        let wantFast: Bool=(*e).fastEnabled && (*e).pulseMode==0 && (*e).dacMode==0 &&
            character==F32(0.0) && !(*e).tubeActive && !(*e).transitioning;
        if wantFast && !(*e).foldedHistoryActive {
            folded_reset(&mut (*e).folded);
            (*e).foldedHistoryActive=true;
            (*e).foldedPriming=0;
            (*e).foldedDepth=(*e).depth;
            (*e).previousFoldedDepth=(*e).depth;
            (*e).foldedFade=128;
        }
        if !wantFast && !(*e).legacyRunning {
            for j in 0..len(&(*e).up) {fir_reset(&mut (*e).up[j]);fir_reset(&mut (*e).down[j]);}
            for j in 0..63 {(*e).alignedL[j]=0.0;(*e).alignedR[j]=0.0;}
            (*e).alignIndex=0;
            (*e).legacyRunning=true;
            (*e).legacyPriming=0;
        }
        for i in 0..frames {
            let mut l: F64 = cast<F64>(lbuf[i]);
            let mut r: F64 = cast<F64>(rbuf[i]);
            if !is_finite(l) { l=0.0; }
            if !is_finite(r) { r=0.0; }
            let inputL: F64 = l;
            let inputR: F64 = r;
            (*e).crossCurrent = (*e).crossCurrent + (xTarget - (*e).crossCurrent) * (*e).controlAlpha;
            (*e).mixCurrent = (*e).mixCurrent + (mTarget - (*e).mixCurrent) * (*e).controlAlpha;
            (*e).gainCurrent = (*e).gainCurrent + (gainTarget - (*e).gainCurrent) * (*e).controlAlpha;
            (*e).characterCurrent=(*e).characterCurrent+(min_f(cast<F64>(character),0.0,1.0)-(*e).characterCurrent)*(*e).controlAlpha;
            (*e).pulseColorCurrent=(*e).pulseColorCurrent+(min_f(cast<F64>(pulseIntensity),0.0,1.0)-(*e).pulseColorCurrent)*(*e).controlAlpha;
            (*e).crossLowL = (*e).crossLowL + (*e).crossAlpha * (l - (*e).crossLowL);
            (*e).crossLowR = (*e).crossLowR + (*e).crossAlpha * (r - (*e).crossLowR);
            let read: USize = ((*e).crossIndex + 64 - (*e).crossSamples) % 64;
            let a: F64 = (*e).delayL[read];
            let b: F64 = (*e).delayR[read];
            (*e).delayL[(*e).crossIndex] = (*e).crossLowL;
            (*e).delayR[(*e).crossIndex] = (*e).crossLowR;
            (*e).crossIndex = ((*e).crossIndex + 1) % 64;
            let feed: F64 = 0.28 * (*e).crossCurrent;
            let norm: F64 = 1.0 / (1.0 + feed);
            l = (l + feed*b) * norm;
            r = (r + feed*a) * norm;
            let mut fastL: F64=0.0;
            let mut fastR: F64=0.0;
            if (*e).foldedHistoryActive {
                folded_push(&mut (*e).folded,l,r);
                if (*e).foldedPriming<384 {(*e).foldedPriming=(*e).foldedPriming+1;}
                if (*e).foldedPriming>=384 {
                    fastL=folded_eval(&(*e).folded,0,(*e).foldedDepth);
                    fastR=folded_eval(&(*e).folded,1,(*e).foldedDepth);
                }
            }
            let mut wl: F64=0.0;
            let mut wr: F64=0.0;
            if (*e).legacyRunning {
                wl=channel_sample(&mut *e,l,0);
                wr=channel_sample(&mut *e,r,1);
            }

            if (*e).legacyRunning {
                let compensation: USize=folded_compensation((*e).depth);
                let ar: USize=((*e).alignIndex+63-compensation)%63;
                let alignedL: F64=(*e).alignedL[ar];
                let alignedR: F64=(*e).alignedR[ar];
                (*e).alignedL[(*e).alignIndex]=wl;
                (*e).alignedR[(*e).alignIndex]=wr;
                (*e).alignIndex=((*e).alignIndex+1)%63;
                wl=alignedL;wr=alignedR;
                if (*e).legacyPriming<384 {(*e).legacyPriming=(*e).legacyPriming+1;}
            }
            let fastReady: Bool=(*e).foldedHistoryActive && (*e).foldedPriming>=384;
            if wantFast && fastReady {
                (*e).fastWeight=min_f((*e).fastWeight+1.0/128.0,0.0,1.0);
            } else if !wantFast && (*e).legacyPriming>=384 {
                (*e).fastWeight=min_f((*e).fastWeight-1.0/128.0,0.0,1.0);
            }
            if (*e).fastWeight>0.0 {
                wl=(1.0-(*e).fastWeight)*wl+(*e).fastWeight*fastL;
                wr=(1.0-(*e).fastWeight)*wr+(*e).fastWeight*fastR;
            }
            if wantFast && (*e).fastWeight>=1.0 {
                (*e).legacyRunning=false;
                (*e).legacyPriming=0;
            }
            if !wantFast && (*e).fastWeight==0.0 && (*e).foldedHistoryActive {
                (*e).foldedHistoryActive=false;
                (*e).foldedPriming=0;
            }
            let mut m: U32 = mode;
            if m > U32(3) {m = U32(3);}
            let mut warmTarget: F64 = 0.0;
            if m >= U32(2) { warmTarget = 1.0; }
            let mut hfTarget: F64 = 0.0;
            if m == U32(1) || m == U32(3) { hfTarget = 1.0; }
            (*e).warmMix = (*e).warmMix + (warmTarget-(*e).warmMix)*(*e).controlAlpha;
            (*e).hfMix = (*e).hfMix + (hfTarget-(*e).hfMix)*(*e).controlAlpha;
            (*e).warmLowL = (*e).warmLowL + (*e).warmAlpha*(wl-(*e).warmLowL);
            (*e).warmLowR = (*e).warmLowR + (*e).warmAlpha*(wr-(*e).warmLowR);
            let warmL: F64 = wl + ((*e).warmLowL-wl)*(0.18*(*e).warmMix);
            let warmR: F64 = wr + ((*e).warmLowR-wr)*(0.18*(*e).warmMix);
            let mut outputL: F64 = warmL;
            let mut outputR: F64 = warmR;
            if (*e).hfAvailable {
                let filteredL: F64 = biquad_process(&mut (*e).hfL,warmL);
                let filteredR: F64 = biquad_process(&mut (*e).hfR,warmR);
                outputL = warmL + (filteredL-warmL)*(*e).hfMix;
                outputR = warmR + (filteredR-warmR)*(*e).hfMix;
            }
            (*e).creative.amounts[0]=(*e).creative.amounts[0]+(min_f(cast<F64>(focus),0.0,1.0)-(*e).creative.amounts[0])*(*e).creative.smoothing;
            (*e).creative.amounts[1]=(*e).creative.amounts[1]+(min_f(cast<F64>(phase),0.0,1.0)-(*e).creative.amounts[1])*(*e).creative.smoothing;
            (*e).creative.amounts[2]=(*e).creative.amounts[2]+(min_f(cast<F64>(tone),0.0,1.0)-(*e).creative.amounts[2])*(*e).creative.smoothing;
            outputL=creative_channel(&mut (*e).creative,outputL,0,phaseMode,m);
            outputR=creative_channel(&mut (*e).creative,outputR,1,phaseMode,m);
            let mut transitionGain: F64=1.0;
            if (*e).transitioning {
                let counter: USize=(*e).transitionCounter;
                if counter<128 {transitionGain=1.0-cast<F64>(counter)/128.0;}
                else {transitionGain=cast<F64>(counter-128)/128.0;}
                (*e).transitionCounter=counter+1;
                if (*e).transitionCounter==128 {
                    if (*e).depth!=(*e).pendingDepth {
                        for j in 2..12 {fir_reset(&mut (*e).up[j]);fir_reset(&mut (*e).down[j]);}
                        (*e).depth=(*e).pendingDepth;
                        for j in 0..63 {(*e).alignedL[j]=0.0;(*e).alignedR[j]=0.0;}
                        (*e).alignIndex=0;
                    }
                    if (*e).pulseMode!=(*e).pendingPulse {
                        (*e).pulses[0]=pulse_create((*e).pendingPulse,0);
                        (*e).pulses[1]=pulse_create((*e).pendingPulse,1);
                        (*e).pulseMode=(*e).pendingPulse;
                    }
                    if (*e).dacMode!=(*e).pendingDac {
                        (*e).dacs[0]=dac_create((*e).pendingDac,0);
                        (*e).dacs[1]=dac_create((*e).pendingDac,1);
                        (*e).dacMode=(*e).pendingDac;
                    }
                }
                if (*e).transitionCounter>=256 {
                    (*e).transitioning=false;
                    (*e).transitionCounter=0;
                }
            }
            let dryL: F64 = (*e).dryL[(*e).dryIndex];
            let dryR: F64 = (*e).dryR[(*e).dryIndex];
            (*e).dryL[(*e).dryIndex] = inputL;
            (*e).dryR[(*e).dryIndex] = inputR;
            (*e).dryIndex = ((*e).dryIndex + 1) % 157;
            lbuf[i] = cast<F32>((dryL*(1.0-(*e).mixCurrent)+outputL*(*e).mixCurrent*transitionGain)*(*e).gainCurrent);
            rbuf[i] = cast<F32>((dryR*(1.0-(*e).mixCurrent)+outputR*(*e).mixCurrent*transitionGain)*(*e).gainCurrent);
        }
        return U32(1);
    }
    export unsafe fn live_process_final(h: OpaqueHandle, left: Ptr<F32>, right: Ptr<F32>, frames: USize,
        crossfeed: F32, mix: F32, gainDb: F32, filterMode: U32,
        phaseMode: U32, focus: F32, phase: F32, tone: F32,
        character: F32, pulseIntensity: F32,
        autoMatch: Bool, limiter: Bool, ceilingDb: F32) -> U32 guarantees(NoAlloc,NoBlocking,NoIO) {
        let mut lb: ForeignBuffer<F32>=foreign(left,frames);
        let mut rb: ForeignBuffer<F32>=foreign(right,frames);
        let mut inputSquare: F64=0.0;
        for i in 0..frames {
            let l: F64=cast<F64>(lb[i]);
            let r: F64=cast<F64>(rb[i]);
            inputSquare=inputSquare+0.5*(l*l+r*r);
        }
        if frames>0 {inputSquare=inputSquare/cast<F64>(frames);}
        let result: U32=live_process_full(h,left,right,frames,crossfeed,mix,gainDb,filterMode,
            phaseMode,focus,phase,tone,character,pulseIntensity);
        if result!=U32(1) {return result;}
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        output_process(&mut (*e).output,left,right,frames,inputSquare,autoMatch,limiter,cast<F64>(ceilingDb));
        return U32(1);
    }

    export unsafe fn live_tube_config(h: OpaqueHandle, typ: U32, topology: U32,
        driveDb: F32, bias: F32, sag: F32, wet: F32) -> U32 {
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        tube_config(&mut (*e).tube,cast<USize>(typ),cast<USize>(topology),
            cast<F64>(driveDb),cast<F64>(bias),cast<F64>(sag),cast<F64>(wet));
        (*e).tubeActive=(*e).tube.desiredType!=0 || (*e).tube.channels[0].oldType!=0 || (*e).tube.channels[0].wet>=1.0e-10;
        return U32(1);
    }

    export unsafe fn live_band_config(h: OpaqueHandle, band: U32, typ: U32, topology: U32,
        driveDb: F32, bias: F32, sag: F32, wet: F32) -> U32 {
        if band>U32(2) {return U32(0);}
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        band_config(&mut (*e).threeBand,cast<USize>(band),cast<USize>(typ),cast<USize>(topology),
            cast<F64>(driveDb),cast<F64>(bias),cast<F64>(sag),cast<F64>(wet));
        return U32(1);
    }
    export unsafe fn live_band_frequency(h: OpaqueHandle, lower: F32, upper: F32) -> U32 {
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        band_frequency(&mut (*e).threeBand,cast<F64>(lower),cast<F64>(upper));
        return U32(1);
    }
    export unsafe fn live_tube_routing(h: OpaqueHandle, routing: U32) -> U32 {
        if routing>U32(1) {return U32(0);}
        let e: Ptr<Engine>=handle_ptr<Engine>(h);
        (*e).tubeRouting=cast<USize>(routing);
        (*e).tubeActive=true;
        return U32(1);
    }

}
