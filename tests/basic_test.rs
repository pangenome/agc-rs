use agc_rs::AGCFile;

#[test]
fn test_create_agc_file() {
    let agc = AGCFile::new();
    assert!(!agc.is_opened());
}

#[test]
fn test_debug_impl() {
    let agc = AGCFile::new();
    let debug_str = format!("{agc:?}");
    assert!(debug_str.contains("AGCFile"));
    assert!(debug_str.contains("is_opened"));
}
