/*
 * ============================================================
 * FILE: bitops.h
 * MỤC ĐÍCH: Cung cấp các macro thao tác bit dùng khắp project.
 *           Dựa trên Linux kernel bitops conventions.
 *
 * CÁC NHÓM MACRO:
 *   1. Kích thước word (BITS_PER_LONG)
 *   2. Tạo bitmask đơn (BIT, BIT_ULL)
 *   3. Tạo bitmask dải liên tục (GENMASK)
 *   4. Đếm số bit cần để biểu diễn (NBITS)
 * ============================================================
 */

/* ── 1. KÍCH THƯỚC WORD ──────────────────────────────────────
 * BITS_PER_LONG: số bit trong một 'long' trên kiến trúc hiện tại.
 *   CONFIG64 được định nghĩa → 64-bit, ngược lại → 32-bit.
 */
#ifdef CONFIG64
#define BITS_PER_LONG 64
#else
#define BITS_PER_LONG 32
#endif /* CONFIG64 */

#define BITS_PER_BYTE           8

/* DIV_ROUND_UP(n, d): chia n cho d, làm tròn lên (ceiling).
 * Ví dụ: DIV_ROUND_UP(10, 3) = 4  (thay vì 3 nếu dùng n/d) */
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* ── 2. BITMASK ĐƠN ─────────────────────────────────────────
 * BIT(nr)    : tạo mask 32-bit với bit thứ nr được bật.
 *              Ví dụ: BIT(3) = 0x00000008 = 0b...00001000
 * BIT_ULL(nr): tương tự nhưng dùng kiểu 64-bit (unsigned long long).
 *              Ví dụ: BIT_ULL(40) = 0x0000010000000000
 */
#define BIT(nr)                 (1U << (nr))
#define BIT_ULL(nr)             (1ULL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)
#define BIT_ULL_MASK(nr)        (1ULL << ((nr) % BITS_PER_LONG_LONG))
#define BIT_ULL_WORD(nr)        ((nr) / BITS_PER_LONG_LONG)

#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

#define BIT_ULL_MASK(nr)        (1ULL << ((nr) % BITS_PER_LONG_LONG))
#define BIT_ULL_WORD(nr)        ((nr) / BITS_PER_LONG_LONG)

/* ── 3. BITMASK DẢI LIÊN TỤC ────────────────────────────────
 * GENMASK(h, l): tạo mask 32-bit với các bit từ vị trí l đến h
 *                đều được bật (inclusive cả hai đầu).
 * Cách tính:
 *   (~0U) << l          → bật tất cả bit từ l trở lên
 *   (~0U) >> (32-h-1)   → bật tất cả bit từ h trở xuống
 *   AND hai cái         → chỉ còn dải [l..h]
 *
 * Ví dụ: GENMASK(4, 1) = 0b00011110 = 0x1E
 *                                (bit 1,2,3,4)
 *
 * LƯU Ý: Macro này dùng ~0U (32-bit). Nếu cần 64-bit,
 *         dùng GENMASK64() định nghĩa trong mm64.h.
 */
#define GENMASK(h, l) \
	(((~0U) << (l)) & (~0U >> (BITS_PER_LONG  - (h) - 1)))

/* ── 4. ĐẾM SỐ BIT CẦN THIẾT (NBITS) ───────────────────────
 * NBITS(n): trả về số bit tối thiểu cần để biểu diễn giá trị n.
 *           Hay còn gọi là floor(log2(n)) + 1.
 * Cách triển khai: chia nhỏ từ 32-bit → 16-bit → 8-bit → ...
 * để đếm dần.
 * Ví dụ: NBITS(256) = 9  (vì 256 = 0x100, cần 9 bit)
 *         NBITS(255) = 8  (vì 255 = 0xFF,  cần 8 bit)
 *
 * Dùng trong mm.h để tính chiều rộng của các trường địa chỉ:
 *   PAGING_ADDR_OFFST_HIBIT = NBITS(PAGING_PAGESZ) - 1
 *   → số bit offset trong một địa chỉ ảo
 */
#define NBITS2(n) ((n&2)?1:0)
#define NBITS4(n) ((n&(0xC))?(2+NBITS2(n>>2)):(NBITS2(n)))
#define NBITS8(n) ((n&0xF0)?(4+NBITS4(n>>4)):(NBITS4(n)))
#define NBITS16(n) ((n&0xFF00)?(8+NBITS8(n>>8)):(NBITS8(n)))
#define NBITS32(n) ((n&0xFFFF0000)?(16+NBITS16(n>>16)):(NBITS16(n)))
#define NBITS(n) (n==0?0:NBITS32(n))

/* EXTRACT_NBITS(nr, h, l): trích bit [h..l] từ giá trị nr.
 * Tương đương GETVAL nhưng không dịch phải. */
#define EXTRACT_NBITS(nr, h, l) ((nr&GENMASK(h,l)) >> l)
