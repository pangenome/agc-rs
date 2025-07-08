use std::path::Path;

#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        include!("agc-rs/src/agc_bridge.h");

        type AGCDecompressor;

        fn create_agc_decompressor() -> UniquePtr<AGCDecompressor>;
        fn open_archive(
            decompressor: Pin<&mut AGCDecompressor>,
            archive_path: &str,
            prefetch: bool,
        ) -> bool;
        fn close_archive(decompressor: Pin<&mut AGCDecompressor>) -> bool;
        fn is_opened(decompressor: &AGCDecompressor) -> bool;

        fn get_contig_string(
            decompressor: Pin<&mut AGCDecompressor>,
            sample_name: &str,
            contig_name: &str,
            start: i32,
            end: i32,
        ) -> Result<String>;

        fn get_contig_length(
            decompressor: &AGCDecompressor,
            sample_name: &str,
            contig_name: &str,
        ) -> i64;

        fn list_samples(decompressor: &AGCDecompressor) -> Vec<String>;
        fn list_contigs(decompressor: &AGCDecompressor, sample_name: &str) -> Vec<String>;
        fn get_no_samples(decompressor: &AGCDecompressor) -> i32;
        fn get_no_contigs(decompressor: &AGCDecompressor, sample_name: &str) -> i32;
    }
}

pub struct AGCFile {
    decompressor: cxx::UniquePtr<ffi::AGCDecompressor>,
}

impl std::fmt::Debug for AGCFile {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("AGCFile")
            .field("is_opened", &self.is_opened())
            .finish()
    }
}

impl AGCFile {
    pub fn new() -> Self {
        Self {
            decompressor: ffi::create_agc_decompressor(),
        }
    }

    pub fn open<P: AsRef<Path>>(&mut self, path: P, prefetch: bool) -> bool {
        let path_str = path.as_ref().to_str().unwrap();
        ffi::open_archive(self.decompressor.pin_mut(), path_str, prefetch)
    }

    pub fn close(&mut self) -> bool {
        ffi::close_archive(self.decompressor.pin_mut())
    }

    pub fn is_opened(&self) -> bool {
        ffi::is_opened(&self.decompressor)
    }

    pub fn get_contig_sequence(
        &mut self,
        sample_name: &str,
        contig_name: &str,
        start: i32,
        end: i32,
    ) -> Result<String, String> {
        ffi::get_contig_string(
            self.decompressor.pin_mut(),
            sample_name,
            contig_name,
            start,
            end,
        )
        .map_err(|e| e.to_string())
    }

    pub fn get_full_contig(
        &mut self,
        sample_name: &str,
        contig_name: &str,
    ) -> Result<String, String> {
        let length = self.get_contig_length(sample_name, contig_name);
        if length <= 0 {
            return Err(format!(
                "Contig {contig_name}@{sample_name} not found or has zero length"
            ));
        }
        self.get_contig_sequence(sample_name, contig_name, 0, (length - 1) as i32)
    }

    pub fn get_contig_length(&self, sample_name: &str, contig_name: &str) -> i64 {
        ffi::get_contig_length(&self.decompressor, sample_name, contig_name)
    }

    pub fn list_samples(&self) -> Vec<String> {
        ffi::list_samples(&self.decompressor)
    }

    pub fn list_contigs(&self, sample_name: &str) -> Vec<String> {
        ffi::list_contigs(&self.decompressor, sample_name)
    }

    pub fn get_no_samples(&self) -> i32 {
        ffi::get_no_samples(&self.decompressor)
    }

    pub fn get_no_contigs(&self, sample_name: &str) -> i32 {
        ffi::get_no_contigs(&self.decompressor, sample_name)
    }
}

impl Default for AGCFile {
    fn default() -> Self {
        Self::new()
    }
}

// AGC is thread-safe internally (it uses mutexes for shared state)
// We need to mark this explicitly because CXX UniquePtr doesn't implement Send/Sync
unsafe impl Send for AGCFile {}
unsafe impl Sync for AGCFile {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_create_agc_file() {
        let agc = AGCFile::new();
        assert!(!agc.is_opened());
    }
}
