module native.xml
import std.native_xml
native {
    export unsafe fn xml_roundtrip(input: Ptr<U8>, count: USize, output: Ptr<U8>, capacity: USize, replace: Bool) -> USize {
        let source: ForeignBuffer<U8>=foreign(input,count);
        let mut doc: XmlDocument=xml_parse(slice(&source,0,count));if !doc.valid { return 0; }
        if replace { xml_replace_value(&mut doc,2,utf8_bytes("Антон<&\t\r\n")); }
        let encoded: Buffer<U8>=xml_write(&doc);if len(&encoded)>capacity { return 0; }
        let mut target: ForeignBuffer<U8>=foreign(output,capacity);for i in 0..len(&encoded) { target[i]=encoded[i]; }return len(&encoded);
    }
    fn main()->Unit {
        let original: Slice<U8>=utf8_bytes("<?xml version='1.0'?><root a='A&amp;&#x410;&#10;' future='keep'><![CDATA[x<y]]><future/></root>");
        let mut doc: XmlDocument=xml_parse(original);assert(doc.valid);assert(len(&doc.tokens)==USize(7));
        let decoded: Buffer<U8>=xml_value(&doc,2);assert(len(&decoded)==USize(5));
        xml_replace_value(&mut doc,2,utf8_bytes("new<&\t\r\n"));
        let changed: Buffer<U8>=xml_value(&doc,2);assert(len(&changed)==USize(8));assert(changed[5]==U8(9));assert(changed[6]==U8(13));assert(changed[7]==U8(10));
        let unknown: Buffer<U8>=xml_value(&doc,3);assert(len(&unknown)==USize(4));
        let bytes: Buffer<U8>=xml_write(&doc);let again: XmlDocument=xml_parse(slice(&bytes,0,len(&bytes)));assert(again.valid);
        let unicode: XmlDocument=xml_parse(utf8_bytes("<Привет имя='Антон'/ >"));assert(!unicode.valid);
        let valid: XmlDocument=xml_parse(utf8_bytes("<Привет имя='Антон'/><!--ok-->"));assert(valid.valid);
        let a: XmlDocument=xml_parse(utf8_bytes("<a x='1' x='2'/>"));assert(!a.valid);
        let b: XmlDocument=xml_parse(utf8_bytes("<a>&#0;</a>"));assert(!b.valid);
        let c: XmlDocument=xml_parse(utf8_bytes("<a></b>"));assert(!c.valid);
        let d: XmlDocument=xml_parse(utf8_bytes("<a/><b/>"));assert(!d.valid);
        let e: XmlDocument=xml_parse(utf8_bytes("<a>]]></a>"));assert(!e.valid);
        let f: XmlDocument=xml_parse(utf8_bytes("<!--bad--comment--><a/>"));assert(!f.valid);
        let g: XmlDocument=xml_parse(utf8_bytes("<!DOCTYPE a><a/>"));assert(!g.valid);
        let h: XmlDocument=xml_parse(utf8_bytes("<a x='<'/>"));assert(!h.valid);
        let j: XmlDocument=xml_parse(utf8_bytes("<a>&unknown;</a>"));assert(!j.valid);
        let k: XmlDocument=xml_parse(utf8_bytes("<?xml version='2.0'?><a/>"));assert(!k.valid);
        print("XML OK");
    }
}
