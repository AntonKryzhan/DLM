use dlm_transport_proof::{check_migration,check_migration_certificate,check_selection,Error};
use std::io::Read;
fn read_certificate(input:impl Read)->Result<Vec<u8>,String>{let mut bytes=Vec::new();input.take(4097).read_to_end(&mut bytes).map_err(|e|e.to_string())?;if bytes.len()>4096{return Err("certificate byte limit".into())}Ok(bytes)}
fn main(){let args=std::env::args().skip(1).collect::<Vec<_>>();let result=(||->Result<String,String>{if args.is_empty(){return Err("usage: dlm-transport-check FILE.nopt [--work N] | --model MESSAGES WINDOW DELIVERY [--work N]".into())}let model=args[0]=="--model";let index=if model{4}else{1};if args.len()<index{return Err("missing model bounds".into())}let mut work=2000000;if args.len()>index{if args.len()!=index+2||args[index]!="--work"{return Err("invalid arguments".into())}work=args[index+1].parse().map_err(|_|"invalid work budget")?;}if model{let values=args[1..4].iter().map(|s|s.parse::<u32>().map_err(|_|"invalid model bound")).collect::<Result<Vec<_>,_>>()?;return check_migration(values[0],values[1],values[2],work).map(|r|r.json()).map_err(|e|e.to_string())}let bytes=read_certificate(std::fs::File::open(&args[0]).map_err(|e|e.to_string())?)?;if bytes.starts_with(b"DOT1"){check_selection(&bytes,work).map(|r|r.json()).map_err(|e|e.to_string())}else{check_migration_certificate(&bytes,work).map(|r|r.json()).map_err(|e|e.to_string())}})();match result{Ok(s)=>println!("{s}"),Err(e)=>{eprintln!("transport proof: {e}");std::process::exit(if e==Error::UnknownWithinBudget.to_string(){3}else{1});}}}

#[cfg(test)]
mod tests {
 use super::*;
 #[test]
 fn certificate_input_is_bounded_even_when_size_is_unknown(){
  struct Infinite(usize);impl Read for Infinite{fn read(&mut self,output:&mut[u8])->std::io::Result<usize>{self.0+=output.len();output.fill(0);Ok(output.len())}}
  let mut infinite=Infinite(0);assert_eq!(read_certificate(&mut infinite).unwrap_err(),"certificate byte limit");assert_eq!(infinite.0,4097);
  for size in [0,36,456,4096]{let bytes=vec![7;size];assert_eq!(read_certificate(bytes.as_slice()).unwrap(),bytes);}
  assert!(read_certificate([0u8;4097].as_slice()).is_err());
 }
}
