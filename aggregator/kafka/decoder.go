package kafka

type versionedDecoder interface {
	decode(pd packetDecoder, version int16) error
}

type packetDecoder interface {
	// Primitives
	getInt8() (int8, error)
	getInt16() (int16, error)
	getInt32() (int32, error)
	getInt64() (int64, error)
	getVarint() (int64, error)
	getUVarint() (uint64, error)
	getFloat64() (float64, error)
	getArrayLength() (int, error)
	getCompactArrayLength() (int, error)
	getBool() (bool, error)
	getEmptyTaggedFieldArray() (int, error)

	// Collections
	getBytes() ([]byte, error)
	getVarintBytes() ([]byte, error)
	getCompactBytes() ([]byte, error)
	getRawBytes(length int) ([]byte, error)
	getString() (string, error)
	getNullableString() (*string, error)
	getCompactString() (string, error)
	getCompactNullableString() (*string, error)
	getCompactInt32Array() ([]int32, error)
	getInt32Array() ([]int32, error)
	getInt64Array() ([]int64, error)
	getStringArray() ([]string, error)

	// Subsets
	remaining() int
	getSubset(length int) (packetDecoder, error)
	peek(offset, length int) (packetDecoder, error) // similar to getSubset, but it doesn't advance the offset
	peekInt8(offset int) (int8, error)              // similar to peek, but just one byte

	// Stacks, see PushDecoder
	push(in pushDecoder) error
	pop() error
}

// PushDecoder is the interface for decoding fields like CRCs and lengths where the validity
// of the field depends on what is after it in the packet. Start them with PacketDecoder.Push() where
// the actual value is located in the packet, then PacketDecoder.Pop() them when all the bytes they
// depend upon have been decoded.
type pushDecoder interface {
	// Saves the offset into the input buffer as the location to actually read the calculated value when able.
	saveOffset(in int)

	// Returns the length of data to reserve for the input of this encoder (e.g. 4 bytes for a CRC32).
	reserveLength() int

	// Indicates that all required data is now available to calculate and check the field.
	// SaveOffset is guaranteed to have been called first. The implementation should read ReserveLength() bytes
	// of data from the saved offset, and verify it based on the data between the saved offset and curOffset.
	check(curOffset int, buf []byte) error
}

// dynamicPushDecoder extends the interface of pushDecoder for uses cases where the length of the
// fields itself is unknown until its value was decoded (for instance varint encoded length
// fields).
// During push, dynamicPushDecoder.decode() method will be called instead of reserveLength()
type dynamicPushDecoder interface {
	pushDecoder
	decoder
}

type decoder interface {
	decode(pd packetDecoder) error
}

// decode takes bytes and a decoder and fills the fields of the decoder from the bytes,
// interpreted using Kafka's encoding rules.
func decode(buf []byte, in decoder) error {
	if buf == nil {
		return nil
	}

	helper := realDecoder{
		raw: buf,
	}
	err := in.decode(&helper)
	if err != nil {
		return err
	}

	if helper.off != len(buf) {
		return PacketDecodingError{"invalid length"}
	}

	return nil
}

func VersionedDecode(buf []byte, in versionedDecoder, version int16) (int, error) {
	if buf == nil {
		return 0, nil
	}

	helper := realDecoder{
		raw: buf,
	}

	err := in.decode(&helper, version)
	if err != nil {
		return helper.off, err
	}

	// if helper.off != len(buf) {
	// 	return helper.off, PacketDecodingError{
	// 		Info: fmt.Sprintf("invalid length (off=%d, len=%d)", helper.off, len(buf)),
	// 	}
	// }

	return helper.off, nil
}