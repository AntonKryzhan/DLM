//! DLN-09: evaluate the existing NetworkSpec in its reference VM, then emit
//! typed DLM bindings to the checked safe codec runtime. No second validator.
use dlm_core::ast::ImportDecl;
use dlm_core::native::Ty;
use dlm_core::native::vm::{Value, Vm};
use dlm_core::{link_program, load_program_with_paths, lower_program, parse_module_with_source, CheckPolicy, FileId, Program, SourceFile};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::fmt::Write as _;
use std::fs;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use std::sync::atomic::{AtomicU64, Ordering};

#[derive(Clone)]
struct Field([u32; 8]);
#[derive(Clone)]
struct Message { header: [u32; 6], fields: Vec<Field> }
pub(super) struct Model { pub(super) words: [u32; 33], messages: Vec<Message>, pub(super) bytes: Vec<u8>, pending: u32 }
struct Options { input: PathBuf, output: PathBuf, module: String, prefix: String, factory: String, minor: Option<u32>, storage: u64, libs: Vec<PathBuf>, force: bool, check: bool }
fn diagnostics(ds: Vec<dlm_core::Diagnostic>) -> String { ds.iter().map(ToString::to_string).collect::<Vec<_>>().join("\n") }
pub(super) fn member(value: &Value, name: &str) -> Result<Value, String> {
    match value { Value::Record(fields) => fields.get(name).map(|v|v.borrow().clone()).ok_or_else(||format!("missing model field {name}")), _ => Err("expected checked model record".into()) }
}
pub(super) fn number(value: &Value, name: &str) -> Result<u64, String> { member(value,name)?.unsigned() }
pub(super) fn sequence(value: &Value) -> Result<Vec<Value>, String> {
    match value { Value::Seq(values) => Ok(values.borrow().iter().map(|v|v.borrow().clone()).collect()), _ => Err("expected checked model array".into()) }
}
pub(super) fn words<const N: usize>(value: &Value) -> Result<[u32; N], String> {
    let values=sequence(value)?;if values.len()!=N { return Err("wrong canonical model array size".into()); }
    let mut result=[0;N];for (out,value) in result.iter_mut().zip(values) { *out=u32::try_from(value.unsigned()?).map_err(|_|"model word is outside U32")?; }Ok(result)
}
pub(super) fn cell(value: Value) -> Rc<RefCell<Value>> { Rc::new(RefCell::new(value)) }
pub(super) fn byte_cells(bytes: &[u8]) -> Rc<RefCell<Vec<Rc<RefCell<Value>>>>> { Rc::new(RefCell::new(bytes.iter().map(|n|cell(Value::U(*n as u64))).collect())) }
pub(super) fn linked(root: dlm_core::ast::Module, programs: Vec<Program>) -> Result<Program,String> {
    let mut library=BTreeMap::new();for program in programs { for module in program.modules { if module.name!=root.name { library.entry(module.name.clone()).or_insert(module); } } }link_program(root,library.into_values().collect()).map_err(diagnostics)
}
pub(super) fn check_library(program: &Program, library: &Path) -> Result<(),String> {
    for module in &program.modules { if module.name.starts_with("std.") {
        let expected=library.join(module.name.replace('.',"/")+".dlm").canonicalize().map_err(|e|e.to_string())?;
        let actual=module.source.path.as_ref().ok_or("standard module has no source path")?.canonicalize().map_err(|e|e.to_string())?;
        if expected!=actual { return Err(format!("input shadows the selected standard library module {}",module.name)); }
    }}Ok(())
}
fn load_model(options: &Options, library: &Path) -> Result<Model,String> {
    let wire=load_program_with_paths(&library.join("std/net_spec_wire.dlm"),&options.libs).map_err(diagnostics)?;
    check_library(&wire,library)?;
    let binary=options.input.extension().and_then(|x|x.to_str())==Some("dns");
    let program=if binary { wire } else {
        let factory=load_program_with_paths(&options.input,&options.libs).map_err(diagnostics)?;
        check_library(&factory,library)?;
        let mut root=factory.root().clone();
        if !root.imports.iter().any(|x|x.path=="std.net_spec_wire") { root.imports.push(ImportDecl{path:"std.net_spec_wire".into()}); }linked(root,vec![factory,wire])?
    };
    let lowered=lower_program(&program,CheckPolicy::default()).map_err(diagnostics)?;
    if !binary {
        let f=lowered.native.unit.functions.iter().find(|f|f.name==options.factory).ok_or_else(||format!("schema factory '{}' was not found",options.factory))?;
        if !f.params.is_empty() || f.ret!=Ty::Record("NetworkSpec".into()) || f.unsafe_ || f.body.is_none() { return Err("schema factory must be a safe no-argument function returning NetworkSpec".into()); }
        for effect in ["NoAlloc","NoBlocking","NoIO","BoundedWork"] { if !f.guarantees.contains(effect) { return Err(format!("schema factory must declare guarantees({effect})")); } }
    }
    let mut vm=Vm::new(&lowered.native).with_step_limit(50_000_000);
    let model=if binary {
        let bytes=read_limited(&options.input,16904)?;let data=byte_cells(&bytes);let decoded=vm.call("network_spec_decode",vec![Value::Slice(data,0,bytes.len())])?;
        let status=number(&decoded,"status")?;if status!=0 { return Err(format!("invalid DNS1 descriptor: status {status}")); }member(&decoded,"spec")?
    }else { vm.call(&options.factory,vec![])? };
    let model_ref=cell(model.clone());let check=vm.call("network_spec_check",vec![Value::Ref(model_ref.clone())])?;
    let status=number(&check,"status")?;if status!=0 { return Err(format!("NetworkSpec rejected: status {status}, component {}, message {}, field {}",number(&check,"component")?,number(&check,"message")?,number(&check,"field")?)); }
    let canonical=words::<33>(&vm.call("network_spec_words",vec![Value::Ref(model_ref.clone())])?)?;
    if canonical[31]!=1 { return Err("proof-required NetworkSpec cannot be silently downgraded: this generator emits an engineering profile with pending obligations".into()); }
    let size=vm.call("network_spec_encoded_size",vec![Value::Ref(model_ref.clone())])?;let count=usize::try_from(number(&size,"count")?).map_err(|_|"descriptor size overflow")?;
    if count>16904 { return Err("descriptor limit exceeded".into()); }let data=byte_cells(&vec![0;count]);let buffer=cell(Value::Seq(data.clone()));
    let encoded=vm.call("network_spec_encode",vec![Value::Ref(model_ref),Value::Ref(buffer),Value::U(0)])?;
    if number(&encoded,"status")?!=0 || number(&encoded,"count")?!=count as u64 { return Err("canonical descriptor encoding failed".into()); }
    let bytes=data.borrow().iter().map(|v|v.borrow().unsigned().map(|n|n as u8)).collect::<Result<Vec<_>,_>>()?;
    let mut messages=Vec::new();let all=sequence(&member(&model,"messages")?)?;
    for message in all.iter().take(canonical[32] as usize) {
        let header=words::<6>(&vm.call("network_message_words",vec![Value::Ref(cell(message.clone()))])?)?;
        let mut fields=Vec::new();for field in sequence(&member(message,"fields")?)?.iter().take(header[5] as usize) { fields.push(Field(words::<8>(&vm.call("network_field_words",vec![Value::Ref(cell(field.clone()))])?)?)); }
        messages.push(Message{header,fields});
    }
    Ok(Model{words:canonical,messages,bytes,pending:number(&check,"pending_proofs")? as u32})
}
pub(super) fn read_limited(path: &Path, maximum: u64) -> Result<Vec<u8>,String> {
    use std::io::Read;
    let mut data=Vec::new();fs::File::open(path).map_err(|e|format!("{}: {e}",path.display()))?.take(maximum+1).read_to_end(&mut data).map_err(|e|e.to_string())?;
    if data.len() as u64>maximum { return Err(format!("{} exceeds {maximum} bytes",path.display())); }Ok(data)
}
fn fnv(bytes: &[u8]) -> u64 { bytes.iter().fold(14695981039346656037u64,|hash,n|(hash^*n as u64).wrapping_mul(1099511628211)) }
fn hex(bytes: &[u8]) -> String { let mut s=String::with_capacity(bytes.len()*2);for n in bytes { write!(s,"{n:02x}").unwrap(); }s }
fn type_name(kind: u32) -> &'static str { match kind {1=>"U8",2=>"U16",3=>"U32",4|8=>"U64",5=>"Bool",9=>"I64",_=>"Slice<U8>"} }
fn u32s(values: &[u32]) -> String { values.iter().map(ToString::to_string).collect::<Vec<_>>().join(",") }
fn field_expression(field: &Field) -> String { format!("MessageField({})",u32s(&field.0)) }
const PURE: &str=" guarantees(NoAlloc,NoBlocking,NoIO,BoundedWork)";
pub(super) fn emit_spec(out: &mut String, model: &Model, prefix: &str) {
    let w=&model.words;writeln!(out,"    fn {prefix}_spec() -> NetworkSpec{PURE} {{").unwrap();
    writeln!(out,"        let mut spec: NetworkSpec=network_spec({},{},{},{});",w[0],w[1],w[2],w[3]).unwrap();
    writeln!(out,"        spec.wire=WireFormat({});spec.transport=TransportRequirements({});",u32s(&w[4..8]),u32s(&w[8..11])).unwrap();
    let memory=((w[14] as u64)<<32)|w[15] as u64;let session_limit=((w[18] as u64)<<32)|w[19] as u64;
    writeln!(out,"        spec.resources=ResourceBounds(TransportBounds({},{},{},{}),{},{},U64({}),{});",w[11],w[12],w[13],memory,w[16],w[17],session_limit,w[20]).unwrap();
    writeln!(out,"        spec.session=SessionContract({},{},{},{},{});spec.security=SecurityRequirements({},{},{},{});spec.proofs=ProofObligations({},{});",w[21],w[22],w[23],w[24],w[25]!=0,w[26],w[27]!=0,w[28]!=0,w[29],w[30],w[31]).unwrap();
    writeln!(out,"        spec.message_count={};",model.messages.len()).unwrap();
    for (i,m) in model.messages.iter().enumerate() {
        let h=&m.header;writeln!(out,"        spec.messages[{i}]=message_schema({},{},{});spec.messages[{i}].first_minor={};spec.messages[{i}].last_minor={};spec.messages[{i}].field_count={};",h[0],h[1],h[2],h[3],h[4],h[5]).unwrap();
        for (j,f) in m.fields.iter().enumerate() { writeln!(out,"        spec.messages[{i}].fields[{j}]={};",field_expression(f)).unwrap(); }
    }
    writeln!(out,"        return spec;\n    }}").unwrap();
}
fn field_bound(field: &Field, wire: u32) -> (u64,u64) {
    let f=&field.0;let (mut low,mut high)=match f[1] {1|5=>(1,1),2=>(2,2),3=>(4,4),4=>(8,8),6|7=>(0,f[4] as u64),8|9=>(1,10),_=>unreachable!()};
    let mut prefix=0;if wire==2 { low+=8;high+=8; }else { if f[1]==6 || f[1]==7 { low+=4;high+=4; }if f[2]!=1 || f[3]!=1 { prefix=4; } }
    (prefix+low*f[2] as u64,prefix+high*f[3] as u64)
}
fn emit_message(out: &mut String, prefix: &str, model: &Model, message: &Message, minor: u32, storage: u64) -> (u64,u64,u32,Vec<Field>) {
    let id=message.header[0];let name=format!("{prefix}Message{id}");let fields=message.fields.iter().filter(|f|f.0[5]<=minor && minor<=f.0[6]).cloned().collect::<Vec<_>>();
    let mut minimum=if model.words[4]==2 {12} else {4};let mut maximum=minimum;let mut values=0u32;
    for f in &fields { let (a,b)=field_bound(f,model.words[4]);minimum+=a;maximum+=b;values+=f.0[3]; }maximum+=model.words[17] as u64;
    writeln!(out,"    record {name} repr(C) {{ inner: CodecMessage; }}\n    record {name}Decoded repr(C) {{ value: {name}; error: CodecError; }}").unwrap();
    writeln!(out,"    fn {prefix}_plan_{id}() -> CodecPlan{PURE} {{\n        let mut plan: CodecPlan=zero<CodecPlan>();plan.message={id};plan.wire=WireFormat({});plan.minor={minor};plan.field_count={};",u32s(&model.words[4..8]),fields.len()).unwrap();
    for (i,f) in fields.iter().enumerate() { writeln!(out,"        plan.fields[{i}]={};",field_expression(f)).unwrap(); }
    writeln!(out,"        plan.maximum_values={values};plan.maximum_wire={maximum};plan.maximum_unknown_fields={};plan.maximum_unknown_bytes={};plan.storage_budget={storage};return plan;\n    }}",model.words[16],model.words[17]).unwrap();
    writeln!(out,"    fn {name}_new() -> {name} {{ return {name}(codec_message({id})); }}").unwrap();
    writeln!(out,"    fn {name}_decode(input: Slice<U8>) -> {name}Decoded {{ let plan: CodecPlan={prefix}_plan_{id}();let decoded: CodecDecoded=codec_decode(&plan,input);return {name}Decoded({name}(move decoded.value),decoded.error); }}").unwrap();
    writeln!(out,"    fn {name}_measure(message: &{name}) -> CodecIo{PURE} {{ let plan: CodecPlan={prefix}_plan_{id}();return codec_measure(&plan,&message.inner); }}").unwrap();
    writeln!(out,"    fn {name}_encode(message: &{name}, output: &mut Buffer<U8>, offset: USize) -> CodecIo{PURE} {{ let plan: CodecPlan={prefix}_plan_{id}();return codec_encode(&plan,&message.inner,output,offset); }}").unwrap();
    writeln!(out,"    fn {name}_equal(left: &{name}, right: &{name}) -> Bool{PURE} {{ let plan: CodecPlan={prefix}_plan_{id}();return codec_equal(&plan,&left.inner,&right.inner); }}").unwrap();
    for f in &fields {
        let fid=f.0[0];let kind=f.0[1];let ty=type_name(kind);let label=format!("{name}_field_{fid}");
        if kind==6 || kind==7 {
            writeln!(out,"    fn {label}_add(message: &mut {name}, value: Slice<U8>) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{id}();return codec_add_bytes(&plan,&mut message.inner,{fid},value); }}").unwrap();
            writeln!(out,"    fn {label}_span(message: &{name}, occurrence: USize) -> CodecSpan{PURE} {{ let plan: CodecPlan={prefix}_plan_{id}();return codec_get_span(&plan,&message.inner,{fid},occurrence); }}").unwrap();
            writeln!(out,"    fn {label}_view(message: &{name}, occurrence: USize) -> Slice<U8> borrows(message){PURE} {{ let span: CodecSpan={label}_span(message,occurrence);return codec_view(&message.inner,span); }}").unwrap();
        }else {
            let result=format!("{name}Field{fid}");writeln!(out,"    record {result} repr(C) {{ value: {ty}; error: CodecError; }}").unwrap();
            let scalar: String=if kind==9 {"zigzag_encode(value)".into()} else if kind==5 {"scalar".into()} else {"cast<U64>(value)".into()};
            let bool_setup=if kind==5 {"let mut scalar: U64=0;if value { scalar=1; }"} else {""};
            writeln!(out,"    fn {label}_add(message: &mut {name}, value: {ty}) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{id}();{bool_setup}return codec_add_scalar(&plan,&mut message.inner,{fid},{scalar}); }}").unwrap();
            let getter: String=if kind==9 {"zigzag_decode(value.value)".into()} else if kind==5 {"value.value!=0".into()} else {format!("cast<{ty}>(value.value)")};
            writeln!(out,"    fn {label}_get(message: &{name}, occurrence: USize) -> {result}{PURE} {{ let plan: CodecPlan={prefix}_plan_{id}();let value: CodecScalar=codec_get_scalar(&plan,&message.inner,{fid},occurrence);return {result}({getter},value.error); }}").unwrap();
            let many: String=if kind==9 {"zigzag_encode(values[index])".into()} else if kind==5 {"scalar".into()} else {"cast<U64>(values[index])".into()};
            let setup=if kind==5 {"let mut scalar: U64=0;if values[index] { scalar=1; }"} else {""};
            writeln!(out,"    fn {label}_add_many(message: &mut {name}, values: Slice<{ty}>) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{id}();let reserved: CodecReservation=codec_reserve_scalars(&plan,&mut message.inner,{fid},len(values));if reserved.error.kind!=0 {{ return reserved.error; }}for index in 0..len(values) {{ {setup}message.inner.values[reserved.start+index]=CodecValue({fid},{kind},{many},0,0); }}return CodecError(0,0,0); }}").unwrap();
        }
    }
    if model.words[7]==1 { writeln!(out,"    fn {name}_add_unknown(message: &mut {name}, value: Slice<U8>) -> CodecError {{ let plan: CodecPlan={prefix}_plan_{id}();return codec_add_unknown(&plan,&mut message.inner,value); }}").unwrap(); }
    (minimum,maximum,values,fields)
}
fn generate(model: &Model, options: &Options, minor: u32, runtime: &[u8], dependencies: &[(String,u64)]) -> Result<(String,String),String> {
    let mut out=format!("// Generated by DLM network-codegen v1. Engineering assurance; protocol proofs remain pending.\nmodule {}\nimport std.net_codec_runtime\n\nnative {{\n",options.module);
    emit_spec(&mut out,model,&options.prefix);let mut plans=Vec::new();
    for message in &model.messages { if message.header[3]<=minor && minor<=message.header[4] { let (minimum,maximum,values,fields)=emit_message(&mut out,&options.prefix,model,message,minor,options.storage);plans.push((message.header[0],minimum,maximum,values,fields)); } }
    if plans.is_empty() { return Err("no messages are active at the selected minor".into()); }
    writeln!(out,"    fn {}_decode(input: Slice<U8>) -> CodecDecoded {{ let id: ProtocolValue=net_read_be(input,0,4);if id.status!=0 {{ return codec_decode_error(id.status,0,0); }}",options.prefix).unwrap();
    for (id,_,_,_,_) in &plans { writeln!(out,"        if id.value==U64({id}) {{ let plan: CodecPlan={}_plan_{id}();return codec_decode(&plan,input); }}",options.prefix).unwrap(); }
    writeln!(out,"        return codec_decode_error(11,0,0);\n    }}\n}}").unwrap();
    if out.len()>2*1024*1024 { return Err("generated source exceeds 2 MiB".into()); }
    let mut manifest=format!("DLM-NET-CODEC-PLAN v1\nassurance engineering\nmodule {}\nprefix {}\nminor {minor}\nstorage {}\ndescriptor {}\ndescriptor_fnv64 {:016x}\nsource_fnv64 {:016x}\nruntime_fnv64 {:016x}\npending {}\nmessages {}\n",options.module,options.prefix,options.storage,hex(&model.bytes),fnv(&model.bytes),fnv(out.as_bytes()),fnv(runtime),model.pending,plans.len());
    for (name,hash) in dependencies { writeln!(manifest,"dependency {name} {hash:016x}").unwrap(); }
    for (id,minimum,maximum,values,fields) in plans { writeln!(manifest,"message {id} {minimum} {maximum} {values} {}",fields.len()).unwrap();for f in fields { writeln!(manifest,"field {}",f.0.iter().map(ToString::to_string).collect::<Vec<_>>().join(" ")).unwrap(); } }
    Ok((out,manifest))
}
fn identifier(text: &str, prefix: bool) -> bool {
    let mut chars=text.chars();let first=match chars.next() {Some(c)=>c,None=>return false};
    text.len()<=if prefix {48} else {128} && (first.is_ascii_alphabetic() || first=='_') && chars.all(|c|c.is_ascii_alphanumeric() || c=='_')
}
fn library(options: &mut Options) -> Result<PathBuf,String> {
    if let Some(paths)=std::env::var_os("DLM_PATH") { options.libs.extend(std::env::split_paths(&paths)); }
    if let Ok(exe)=std::env::current_exe() { if let Some(parent)=exe.parent().and_then(Path::parent) { options.libs.push(parent.join("lib")); } }
    options.libs.push(PathBuf::from("lib"));
    options.libs.iter().find(|p|p.join("std/net_spec_wire.dlm").is_file() && p.join("std/net_codec_runtime.dlm").is_file()).cloned().ok_or_else(||"NetworkSpec/codec standard library was not found; pass --lib DLM-main/lib or the rebuilt SDK lib".into())
}
struct OutputLock(PathBuf);
impl Drop for OutputLock { fn drop(&mut self) { let _=fs::remove_file(&self.0); } }
pub(super) fn write_bundle(bundle: &[(PathBuf,Vec<u8>)], force: bool, check: bool) -> Result<(),String> {
    let first=&bundle[0].0;let parent=first.parent().filter(|p|!p.as_os_str().is_empty()).unwrap_or_else(||Path::new("."));
    let lock_path=parent.join(format!(".{}.codegen-lock",first.file_name().unwrap().to_string_lossy()));
    let _lock=if check { if lock_path.exists() { return Err("bundle generation is in progress".into()); }None } else {
        fs::create_dir_all(parent).map_err(|e|e.to_string())?;let file=fs::OpenOptions::new().write(true).create_new(true).open(&lock_path).map_err(|e|format!("cannot acquire generation lock: {e}"))?;drop(file);Some(OutputLock(lock_path))
    };
    for (path,bytes) in bundle {
        let old=match fs::symlink_metadata(path) {
            Ok(meta) => { if !meta.is_file() || meta.file_type().is_symlink() { return Err(format!("refusing non-regular output {}",path.display())); }Some(fs::read(path).map_err(|e|format!("cannot read output {}: {e}",path.display()))?) },
            Err(e) if e.kind()==std::io::ErrorKind::NotFound => None,
            Err(e) => return Err(e.to_string()),
        };
        if old.as_deref()==Some(bytes.as_slice()) { continue; }
        if check { return Err(format!("generated bundle differs: {}",path.display())); }
        if old.is_some() && !force { return Err(format!("output exists and differs: {}; use --force for intentional regeneration",path.display())); }
    }
    if check { return Ok(()); }
    static SERIAL: AtomicU64=AtomicU64::new(0);let serial=SERIAL.fetch_add(1,Ordering::Relaxed);let suffix=format!("{}.{}",std::process::id(),serial);let mut staged=Vec::new();let mut backups=Vec::new();let mut installed=Vec::new();
    let result=(|| {
        for (path,bytes) in bundle { let parent=path.parent().filter(|p|!p.as_os_str().is_empty()).unwrap_or_else(||Path::new("."));fs::create_dir_all(parent).map_err(|e|e.to_string())?;
            let temporary=parent.join(format!(".{}.{}.new",path.file_name().unwrap().to_string_lossy(),suffix));let mut file=fs::OpenOptions::new().write(true).create_new(true).open(&temporary).map_err(|e|e.to_string())?;staged.push((path.clone(),temporary));use std::io::Write;file.write_all(bytes).and_then(|_|file.sync_all()).map_err(|e|e.to_string())?;
        }
        for (path,temporary) in &staged { if path.exists() { let backup=path.with_file_name(format!(".{}.{}.previous",path.file_name().unwrap().to_string_lossy(),suffix));if backup.exists() { return Err("generation backup name collision".into()); }fs::rename(path,&backup).map_err(|e|e.to_string())?;backups.push((path.clone(),backup)); }fs::rename(temporary,path).map_err(|e|e.to_string())?;installed.push(path.clone()); }Ok(())
    })();
    if result.is_err() { for path in installed.iter().rev() { let _=fs::remove_file(path); }for (path,backup) in backups.iter().rev() { let _=fs::rename(backup,path); } }
    else { for (_,backup) in &backups { let _=fs::remove_file(backup); } }
    for (_,temporary) in &staged { let _=fs::remove_file(temporary); }result
}
fn execute(args: &[String]) -> Result<(),String> {
    let usage="usage: dlm network-codegen SCHEMA.dlm|SPEC.dns --module generated.name --prefix Name -o codec.dlm [--factory network_schema] [--minor N] [--storage BYTES] [--lib DIR] [--force|--check]";
    if args.is_empty() { return Err(usage.into()); }
    let mut options=Options{input:PathBuf::from(&args[0]),output:PathBuf::new(),module:String::new(),prefix:String::new(),factory:"network_schema".into(),minor:None,storage:8*1024*1024,libs:vec![],force:false,check:false};let mut index=1;
    while index<args.len() { let flag=args[index].as_str();index+=1;if flag=="--force" { options.force=true;continue; }if flag=="--check" { options.check=true;continue; }
        let value=args.get(index).ok_or_else(||format!("{flag} expects a value\n{usage}"))?;index+=1;
        match flag {"--module"=>options.module=value.clone(),"--prefix"=>options.prefix=value.clone(),"-o"|"--output"=>options.output=PathBuf::from(value),"--factory"=>options.factory=value.clone(),"--minor"=>options.minor=Some(value.parse().map_err(|_|"invalid minor")?),"--storage"=>options.storage=value.parse().map_err(|_|"invalid storage budget")?,"--lib"=>options.libs.push(PathBuf::from(value)),_=>return Err(format!("unsupported option {flag}\n{usage}"))}
    }
    if !options.module.split('.').all(|p|identifier(p,false)) || !identifier(&options.prefix,true) || !identifier(&options.factory,false) || options.output.extension().and_then(|s|s.to_str())!=Some("dlm") || !(256..=67108864).contains(&options.storage) || (options.check && options.force) { return Err(usage.into()); }
    let library=library(&mut options)?;let runtime_path=library.join("std/net_codec_runtime.dlm");let runtime=read_limited(&runtime_path,1024*1024)?;let model=load_model(&options,&library)?;let minor=options.minor.unwrap_or(model.words[2]);
    if minor<model.words[3] || minor>model.words[2] { return Err("selected minor is outside the accepted version window".into()); }
    let mut dependencies=Vec::new();
    for name in ["net_codec_runtime","net_spec","net_spec_wire","net_transport_policy","net_bytes","native_protocol","native_binary"] { let relative=format!("std/{name}.dlm");let bytes=read_limited(&library.join(&relative),1024*1024)?;dependencies.push((relative,fnv(&bytes))); }
    let (source,manifest)=generate(&model,&options,minor,&runtime,&dependencies)?;
    let parsed=parse_module_with_source(&source,SourceFile::named(FileId::ROOT,&options.output)).map_err(diagnostics)?;
    let runtime_program=load_program_with_paths(&runtime_path,&options.libs).map_err(diagnostics)?;check_library(&runtime_program,&library)?;let program=linked(parsed,vec![runtime_program])?;let lowered=lower_program(&program,CheckPolicy::default()).map_err(diagnostics)?;
    let mut vm=Vm::new(&lowered.native).with_step_limit(50_000_000);
    for message in &model.messages { if message.header[3]<=minor && minor<=message.header[4] { let plan=vm.call(&format!("{}_plan_{}",options.prefix,message.header[0]),vec![])?;let checked=vm.call("codec_plan_check",vec![Value::Ref(cell(plan))])?;if number(&checked,"kind")?!=0 { return Err("generated codec plan failed the existing DLM runtime validator".into()); } } }
    let bundle=vec![(options.output.clone(),source.into_bytes()),(PathBuf::from(format!("{}.dns",options.output.display())),model.bytes),(PathBuf::from(format!("{}.codec",options.output.display())),manifest.into_bytes())];
    let mut protected=vec![options.input.canonicalize().map_err(|e|e.to_string())?];for (relative,_) in &dependencies { protected.push(library.join(relative).canonicalize().map_err(|e|e.to_string())?); }
    for (path,_) in &bundle { if let Ok(target)=path.canonicalize() { if protected.contains(&target) { return Err("generated output cannot replace the schema input or codec runtime dependency".into()); } } }
    write_bundle(&bundle,options.force,options.check)?;
    println!("Network codec {}: {} (minor {minor}, pending obligations {})",if options.check {"bundle reproduced"} else {"generated"},options.output.display(),model.pending);Ok(())
}
pub fn run(args: &[String]) { if let Err(error)=execute(args) { eprintln!("network-codegen: {error}");std::process::exit(1); } }

pub(super) fn checked_schema(input: &Path, libs: &[PathBuf]) -> Result<(Model,PathBuf,Vec<PathBuf>),String> {
    let mut options=Options{input:input.to_owned(),output:PathBuf::new(),module:String::new(),prefix:String::new(),factory:"network_schema".into(),minor:None,storage:8388608,libs:libs.to_vec(),force:false,check:false};let selected=library(&mut options)?;let model=load_model(&options,&selected)?;Ok((model,selected,options.libs))
}
