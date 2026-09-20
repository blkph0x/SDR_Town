// Test helpers outside #[test] functions are not covered by the clippy.toml
// test allowances.
#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

//! The encoder is meant for small systems: after construction, encoding and
//! synthesizing a full transmission must not touch the allocator. This test
//! pins that guarantee with a counting global allocator.

use std::alloc::{GlobalAlloc, Layout, System};
use std::sync::atomic::{AtomicUsize, Ordering};

use sstv::{Encoder, Mode, RgbPixel, Synthesizer};

/// Wraps the system allocator, counting every allocation and reallocation.
struct CountingAllocator;

static ALLOCATIONS: AtomicUsize = AtomicUsize::new(0);

unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        ALLOCATIONS.fetch_add(1, Ordering::SeqCst);
        unsafe { System.alloc(layout) }
    }

    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        ALLOCATIONS.fetch_add(1, Ordering::SeqCst);
        unsafe { System.realloc(ptr, layout, new_size) }
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
}

#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

#[test]
fn encoding_does_not_allocate_after_construction() {
    let mode = Mode::Robot36;
    let (width, height) = (mode.image_width(), mode.image_height());
    let image =
        (0..width * height).map(|i| RgbPixel::new(i as u8, (i >> 8) as u8, (i >> 16) as u8));

    let encoder = Encoder::new(mode, image).expect("construct encoder");
    let synthesizer = Synthesizer::new(encoder, 48_000);

    let before = ALLOCATIONS.load(Ordering::SeqCst);
    // Consume the entire transmission without allocating on our side. The
    // samples pass through `black_box` so the loop cannot be optimized away.
    let mut samples = 0usize;
    for sample in synthesizer {
        samples += 1;
        std::hint::black_box(sample);
    }
    let after = ALLOCATIONS.load(Ordering::SeqCst);

    assert!(samples > 0, "expected a non-empty transmission");
    assert_eq!(
        after - before,
        0,
        "encoding allocated {} times after construction",
        after - before
    );
}
