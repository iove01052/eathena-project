#ifndef __BASEPACKET_H__
#define __BASEPACKET_H__

#include "basetypes.h"
#include "basearray.h"


///////////////////////////////////////////////////////////////////////////////
NAMESPACE_BEGIN(basics)
///////////////////////////////////////////////////////////////////////////////

/*

	<< Packets >>

Concept:
	A packet has a set of fields.
	Every field of the packet is a public object in the packet.
	A field is identified by id and is linked internally to a field handler.
	The field handler has the offsets and lengths of every field (set of field 
	positions) and a buffer handler (handles and probably owns the data).

	Current implementations of the set of positions use ids as indexes.

           .---.  .---.  .---.  .-----.  .-----.  .-----.
.----------| B |--| W |--| L |--| Str |--| Arr |--| ... |--.
| IPacket  `---'  `---'  `---'  `-----'  `-----'  `-----'  |
|             \      \      \     /        /        /      |
|              \______\______\___/________/________/       |
|                              |                           |
|                             \|/                          |
|                              `                           |
|             .-------------------------------------.      |
|             | IFieldHandler                       |      |
|             |                                     |      |
|             |   .-----------------------------.   |      |
|             |   |  set of field positions     |   |      |
|             |   `-----------------------------'   |      |
|             |                                     |      |
|             |   .-----------------------------.   |      |
|             |   |  buffer handler and owner   |   |      |
|             |   `-----------------------------'   |      |
|             |                                     |      |
|             `-------------------------------------'      |
|                                                          |
`----------------------------------------------------------'

	IPacket - public packet interface

	B,W,L,Str,Arr,... - public fields, linked internally to the field handler

	IFieldHandler - field handler





Composite field:
	TODO

                      .---------------.
.---------------------| sub-fields... |----.
| ACompositeField     `---------------'    |
|                             |            |
|                            \|/           |
|                             `            |
|   .----------------------------------.   |
|   | CChildIdxFieldHandler            |   |
|   |                                  |   |
|   |   .--------------------------.   |   |
|   |   |  set of field positions  |   |   |
|   |   `--------------------------'   |   |
|   |                                  |   |
|   |   .--------------------------.   |   |
|   |   | CChildBuffer             |   |   |
|   |   `--------------------------'   |   |
|   |                                  |   |
|   `----------------------------------'   |
|                                          |
`------------------------------------------'

	ACompositeField - a field that is composed of other sub-fields

	sub-fields... - public fields, linked internaly to the field handler of ACompositeField

	CChildIdxFieldHandler - an IFieldHandler that uses a CChildBuffer for buffer

	CChildBuffer - buffer that is linked to another IFieldHandler for data

--------------------------------------------------

	TODO

	CFieldLimArray - array with a limited number of sub-fields
	CFieldDynArray - array with a dynamic number of sub-fields
	CLimPacket - dynamic-size packet with a fixed-size buffer
	CDynPacket - dynamic-size packet
	more test cases



	TESTING

	CFieldCString - nul-terminated dynamic-size string
	CFieldFixArray - array with a fixed number of sub-fields

*/



/// test packet functions
void test_packet(void);

/// The client can handle packet sizes up to 20480, 20k.
#define CLIENTPACKET_MAX 20480





///////////////////////////////////////////////////////////////////////////////
// Field handler
///////////////////////////////////////////////////////////////////////////////
namespace NFieldHandler {





/// Field handler interface
/// Ids can be indexes, offsets, or anything we want. Depends on the implementation.
///
/// @interface
class IFieldHandler
{
public:
	virtual ~IFieldHandler()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	/// Sets the offset and length of the target id.
	virtual void setup(size_t id, size_t off, size_t len)=0;

	/// Returns the data of the target id.
	virtual const uint8* data(size_t id) const=0;

	/// Returns the data of the target id.
	virtual uint8* data(size_t id)=0;

	/// Returns the offset of the target id.
	virtual size_t offset(size_t id) const=0;

	/// Returns the length of the target id.
	virtual size_t length(size_t id) const=0;

	/// Returns the capcity of the target id.
	virtual size_t capacity(size_t id) const=0;

	/// Inserts n bytes in the offset of the target id.
	/// On success, out contains n.
	/// On failure, out contains the max it can grow.
	virtual bool insert(size_t id, size_t off, size_t n, size_t& out)=0;

	/// Removes n bytes in the offset of the target id.
	/// On success, out contains n.
	/// On failure, out contains the max it can shrink.
	virtual bool remove(size_t id, size_t off, size_t n, size_t& out)=0;

	/// Resizes the target id.
	/// On success out contains sz.
	/// On failure out contains the maximum size if increasing or
	/// the minimum size if decreasing.
	virtual bool resize(size_t id, size_t sz, size_t& out)=0;
};



/// Position of a field in the buffer
struct SPos
{
	SPos()
	:	off(0)
	,	len(0)
	{}

	inline operator size_t() const
	{
		return this->off + this->len;
	}

	size_t off;
	size_t len;
};



/// Fixed set of positions.
template<size_t SZ>
class CFixPosSet
{
public:
	CFixPosSet()
	{}

	bool contains(size_t id) const
	{
		return ( id < SZ );
	}
	bool add(size_t id)
	{
		if( id >= SZ )
			return false;
		this->_pos[id].off = 0;
		this->_pos[id].len = 0;
		return true;
	}
	const SPos& operator ()(size_t id) const
	{
		return this->_pos[id];
	}
	SPos& operator ()(size_t id)
	{
		return this->_pos[id];
	}
private:
	SPos _pos[SZ];
};



/// Dynamic set of positions.
class CDynPosSet
{
public:
	CDynPosSet()
	:	_pos()
	{}

	bool contains(size_t id) const
	{
		return (id < this->_pos.size() );
	}
	bool add(size_t id)
	{
		if( id >= this->_pos.size() && !this->_pos.resize(id + 1) )
			return false;
		this->_pos[id].off = 0;
		this->_pos[id].len = 0;
		return true;
	}
	const SPos& operator ()(size_t id) const
	{
		return this->_pos(id);
	}
	SPos& operator ()(size_t id)
	{
		return this->_pos(id);
	}
private:
	vector<SPos> _pos;
};



/// vector with a fixed size buffer
template<size_t SZ,typename T>
class vector_fx : public vector<T,allocator_w_st<T> >
{
public:
	vector_fx<SZ,T>()
	:	vector<T,allocator_w_st<T> >()
	{
		this->cBuf = this->cWpp = (T*)this->_arr;
		this->cEnd = (T*)this->_arr + SZ;
	}
private:
	T _arr[SZ];
};



/// Buffer with a fixed size.
template<size_t SZ>
class CFixBuffer
{
public:
	CFixBuffer()
	:	_buf()
	{}
	CFixBuffer(const uint8* buf, size_t sz)
	:	_buf()
	{
		sz = min(sz, SZ);
		this->_buf.resize(sz);
		memset(this->_buf.begin(), 0, SZ);
		if( buf )
			memcpy(this->_buf.begin(), buf, sz);
	}
	const uint8* data(size_t off=0) const
	{
		return this->_buf.begin() + off;
	}
	uint8* data(size_t off=0)
	{
		return this->_buf.begin() + off;
	}
	size_t length() const
	{
		return this->_buf.length();
	}
	size_t capacity() const
	{
		return this->_buf.capacity();
	}
	bool insert(size_t off, size_t n, size_t& out)
	{
		if( !this->_buf.resize(this->length() + n) )
		{// failed
			out = this->capacity() - this->length();
			return false;
		}
		this->_buf.move(off + n, off, this->length() - n);
		out = n;
		return true;
	}
	bool remove(size_t off, size_t n, size_t& out)
	{
		if( !this->_buf.removeindex(off, n) )
		{// failed
			out = 0;
			return false;
		}
		out = n;
		return false;
	}
	bool resize(size_t sz, size_t& out)
	{
		if( !this->_buf.resize(sz) )
		{// failed
			out = this->capacity();
			return false;
		}
		out = sz;
		return true;
	}
private:
	vector_fx<SZ,uint8> _buf;
};



/// Buffer with a dynamic size.
class CDynBuffer
{
public:
	CDynBuffer()
	:	_buf()
	{}
	CDynBuffer(const uint8* buf, size_t sz)
	:	_buf()
	{
		this->_buf.resize(sz);
		if( buf )
			memcpy(this->_buf.begin(), buf, sz);
		else
			memset(this->_buf.begin(), 0, sz);
	}
	const uint8* data(size_t off=0) const
	{
		return this->_buf.begin() + off;
	}
	uint8* data(size_t off=0)
	{
		return this->_buf.begin() + off;
	}
	size_t length() const
	{
		return this->_buf.length();
	}
	size_t capacity() const
	{
		return this->_buf.capacity();
	}
	bool insert(size_t off, size_t n, size_t& out)
	{
		if( !_buf.resize(length() + n) )
		{// failed
			out = this->capacity() - this->length();
			return false;
		}
		this->_buf.move(off + n, off, this->length() - n);
		out = n;
		return true;
	}
	bool remove(size_t off, size_t n, size_t& out)
	{
		if( !this->_buf.removeindex(off, n) )
		{// failed
			out = 0;
			return false;
		}
		out = n;
		return false;
	}
	bool resize(size_t sz, size_t& out)
	{
		if( !this->_buf.resize(sz) )
		{// failed
			out = this->capacity();
			return false;
		}
		out = sz;
		return true;
	}
private:
	vector<uint8> _buf;
};



/// Buffer backed by a field handler.
class CChildBuffer
{
public:
	CChildBuffer()
	:	_buf(NULL)
	{}
	void Init(IFieldHandler* buf, size_t id, size_t off, size_t len)
	{
		this->_id = id;
		this->_buf = buf;
		if( buf )
			buf->setup(id, off, len);
	}
	const uint8* data(size_t off=0) const
	{
		if( this->_buf )
			return this->_buf->data(this->_id) + off;
		return NULL;
	}
	uint8* data(size_t off=0)
	{
		if( this->_buf )
			return this->_buf->data(this->_id) + off;
		return NULL;
	}
	size_t length() const
	{
		if( this->_buf )
			return this->_buf->length(this->_id);
		return 0;
	}
	size_t capacity() const
	{
		if( this->_buf )
			return this->_buf->capacity(this->_id);
		return 0;
	}
	bool insert(size_t off, size_t n, size_t& out)
	{
		if( this->_buf )
			return this->_buf->insert(this->_id, off, n, out);
		out = 0;
		return false;
	}
	bool remove(size_t off, size_t n, size_t& out)
	{
		if( this->_buf )
			return this->_buf->remove(this->_id, off, n, out);
		out = 0;
		return false;
	}
	bool resize(size_t sz, size_t& out)
	{
		if( this->_buf )
			return this->_buf->resize(this->_id, sz, out);
		return false;
	}
private:
	size_t _id;
	IFieldHandler* _buf;
};



/// Field handler where ids are indexes in a position array.
///
/// @param P Positions
/// @param B Buffer
template<class P, class B>
class CIdxFieldHandler : public IFieldHandler
{
public:
	CIdxFieldHandler()
	:	_pos()
	,	_buf()
	{}
	CIdxFieldHandler(const uint8* buf, size_t sz)
	:	_pos()
	,	_buf(buf, sz)
	{}
	virtual ~CIdxFieldHandler()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	/// Sets the offset and length of the target id.
	virtual void setup(size_t id, size_t off, size_t len)
	{
		if( !this->_pos.contains(id) && !this->_pos.add(id) )
			return;
		this->_pos(id).off = off;
		this->_pos(id).len = len;
		size_t out;
		if( this->_buf.data() && this->_buf.length() < this->_pos(id) && !this->_buf.resize(this->_pos(id), out) )
		{
			//## critical error, bad packet definition?
			printf("[Debug] CIdxFieldHandler::setup(%u,%u,%u): resize failed - bad packet definition?\n", (uint)id, (uint)off, (uint)len);
			this->_pos(id).off = 0;
			this->_pos(id).len = 0;
		}
	}

	/// Returns the data of the target id.
	virtual const uint8* data(size_t id) const
	{
		if( !this->_pos.contains(id) )
			return NULL;
		return this->_buf.data(_pos(id).off);
	}

	/// Returns the data of the target id.
	virtual uint8* data(size_t id)
	{
		if( !this->_pos.contains(id) )
			return NULL;
		return this->_buf.data(_pos(id).off);
	}

	/// Returns the offset of the target id.
	virtual size_t offset(size_t id) const
	{
		if( !this->_pos.contains(id) )
			return 0;
		return this->_pos(id).off;
	}

	/// Returns the length of the target id.
	virtual size_t length(size_t id) const
	{
		if( !this->_pos.contains(id) )
			return 0;
		return this->_pos(id).len;
	}

	/// Returns the capacity of the target id.
	virtual size_t capacity(size_t id) const
	{
		if( !this->_pos.contains(id) )
			return 0;
		return this->_pos(id).len + this->_buf.capacity() - this->_buf.length();
	}

	/// Inserts n bytes in the offset of the target id.
	/// On success, out contains n.
	/// On failure, out contains the max it can grow.
	virtual bool insert(size_t id, size_t off, size_t n, size_t& out)
	{
		if( !this->_pos.contains(id) )
			return false;
		return this->_buf.insert(off, n, out);
	}

	/// Removes n bytes in the offset of the target id.
	/// On success, out contains sz.
	/// On failure, out contains the max it can shrink.
	virtual bool remove(size_t id, size_t off, size_t n, size_t& out)
	{
		if( !this->_pos.contains(id) )
			return false;
		return this->_buf.remove(off, n, out);
	}

	/// Tries to resize the target id.
	/// On success out contains the new size.
	/// On failure out contains the maximum size if increasing or
	/// the minimum size if decreasing.
	virtual bool resize(size_t id, size_t sz, size_t& out)
	{
		if( !this->_pos.contains(id) )
		{
			out = 0;
			return false;
		}
		return this->_buf.resize(sz, out);
	}

	/// Returns the data of this buffer.
	const uint8* data() const
	{
		return this->_buf.data();
	}

	/// Returns the data of this buffer.
	uint8* data()
	{
		return this->_buf.data();
	}

	/// Returns the length of this buffer.
	size_t length() const
	{
		return this->_buf.length();
	}

protected:
	///////////////////////////////////////////////////////////////////////////
	/// Positions
	P _pos;

	/// Buffer
	B _buf;
};



/// Field buffer that uses another field buffer for data.
/// Ids are indexes in the position array.
///
/// @param P Positions
template<class P>
class CChildIdxFieldBuffer : public CIdxFieldHandler<P,CChildBuffer>
{
public:
	CChildIdxFieldBuffer()
	:	CIdxFieldHandler<P,CChildBuffer>()
	{}
	virtual ~CChildIdxFieldBuffer()
	{}

public:
	/// Initializes this child buffer.
	void Init(IFieldHandler* buf, size_t id, size_t off, size_t len)
	{
		this->_buf.Init(buf, id, off, len);
	}
};





}// end namespace NFieldHandler
///////////////////////////////////////////////////////////////////////////////
// Packets
///////////////////////////////////////////////////////////////////////////////





/// Packet interface
///
/// @interface
class IPacket
{
public:
	virtual ~IPacket()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	/// Returns the data of this packet.
	virtual const uint8* data() const=0;

	/// Returns the length of this packet.
	virtual size_t length() const=0;
};



/// Fixed-size packet.
/// Must contain fixed-size fields only.
///
/// @param NUM Number of fields 
/// @param SZ Size of the buffer
template<size_t NUM, size_t SZ=CLIENTPACKET_MAX>
class CFixPacket : public IPacket
{
public:
	CFixPacket()
	:	_buf(NULL, SZ)
	{}
	CFixPacket(const uint8* buf, size_t sz)
	:	_buf(NULL, SZ)
	{
		if( buf )
			memcpy(this->_buf.data(), buf, min(sz, SZ));
	}
	virtual ~CFixPacket()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	/// Returns the data of this packet.
	virtual const uint8* data() const
	{
		return this->_buf.data();
	}

	/// Returns SZ.
	virtual size_t length() const
	{
		return SZ;
	}

protected:
	///////////////////////////////////////////////////////////////////////////
	/// Field handler
	NFieldHandler::CIdxFieldHandler< NFieldHandler::CFixPosSet<NUM>,NFieldHandler::CFixBuffer<SZ> > _buf;
};





///////////////////////////////////////////////////////////////////////////////
// Fields
///////////////////////////////////////////////////////////////////////////////





/// Abstract field
class AField
{
protected:
	AField()
	:	_buf(NULL)
	,	_id(~size_t(0))
	{}
	virtual ~AField()
	{
		this->_buf = NULL;
		this->_id = ~size_t(0);
	}

public:
	///////////////////////////////////////////////////////////////////////////
	/// Initializes the field.
	///
	/// @param len Maxiumu length the field can take
	void Init(NFieldHandler::IFieldHandler* buf, size_t id, size_t off, size_t len=0)
	{
		this->_id = id;
		this->_buf = buf;
		if( buf )
			buf->setup(id, off, this->_initlength(len));// length based on contents
	}

protected:
	/// Returns the length of the field.
	/// When no data is available, it returns the minimum length.
	/// When data is available, it returns the length based on the contents.
	///
	/// @param len Maximum length of the field
	/// @abstract
	virtual size_t _initlength(size_t len)=0;

protected:
	///////////////////////////////////////////////////////////////////////////
	/// Data buffer
	NFieldHandler::IFieldHandler* _buf;

	/// Id of this field
	size_t _id;
};



/// Abstract composite field
///
/// @param P Positions
template<class P>
class ACompositeField
{
protected:
	ACompositeField()
	:	_buf()
	{}
	virtual ~ACompositeField()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	/// Initializes the field.
	///
	/// @param len Maximum length of the composite field
	void Init(NFieldHandler::IFieldHandler* buf, size_t id, size_t off, size_t len=0)
	{
		this->_buf.Init(buf, id, off, this->_initlength(len));//buffer has no data - minimum length
		this->_buf.Init(buf, id, off, this->_initlength(len));//buffer has data - length based on contents
	}

protected:
	/// Returns the length of the composite field.
	/// When no data is available, it returns the minimum length.
	/// When data is available, it returns the length based on the contents.
	///
	/// @param len Maximum length of the composite field
	/// @abstract
	virtual size_t _initlength(size_t len)=0;

protected:
	///////////////////////////////////////////////////////////////////////////
	/// Field handler
	NFieldHandler::CChildIdxFieldBuffer<P> _buf;
};



/// Byte field
class CFieldB : public AField
{
public:
	CFieldB()
	:	AField()
	{}
public:
	///////////////////////////////////////////////////////////////////////////
	operator uint8() const
	{
		return this->operator()();
	}
	uint8 operator ()() const
	{
		if( this->_buf )
			return this->_buf->data(this->_id)[0];
		return 0;
	}

	CFieldB& operator =(const CFieldB& f)
	{
		if( this->_buf && f._buf )
			this->_buf->data(this->_id)[0] = f._buf->data(f._id)[0];
		return *this;
	}
	uint8 operator =(uint8 val)
	{
		if( this->_buf )
			this->_buf->data(this->_id)[0] = val;
		return val;
	}

private:
	///////////////////////////////////////////////////////////////////////////
	/// Returns 1.
	virtual size_t _initlength(size_t len)
	{
		return 1;
	}
};



/// Word field
class CFieldW : public AField
{
public:
	CFieldW()
	:	AField()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	operator uint16() const
	{
		return this->operator ()();
	}
	uint16 operator ()() const
	{
		if( this->_buf )
		{
			uint8* buf = this->_buf->data(this->_id);
			return
				( uint16(buf[0])         )|
				( uint16(buf[1]) << 0x08 );
		}
		return 0;
	}

	CFieldW& operator =(const CFieldW& f)
	{
		if( this->_buf && f._buf )
			memcpy(this->_buf->data(this->_id), f._buf->data(f._id), 2);
		return *this;
	}
	uint16 operator =(uint16 val)
	{
		if( this->_buf )
		{
			uint8* buf = this->_buf->data(this->_id);
			buf[0] = uint8( (val & 0x00FF)         );
			buf[1] = uint8( (val & 0xFF00) >> 0x08 );
		}
		return val;
	}

private:
	/// Returns 2.
	virtual size_t _initlength(size_t len)
	{
		return 2;
	}
};



/// Long field
class CFieldL : public AField
{
public:
	CFieldL()
	:	AField()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	operator uint32() const
	{
		return this->operator ()();
	}
	uint32 operator ()() const
	{
		if( this->_buf )
		{
			uint8* buf = this->_buf->data(this->_id);
			return
				( uint32(buf[0])         )|
				( uint32(buf[1]) << 0x08 )|
				( uint32(buf[2]) << 0x10 )|
				( uint32(buf[3]) << 0x18 );
		}
		return 0;
	}

	CFieldL& operator =(const CFieldL& f)
	{
		if( this->_buf && f._buf )
			memcpy(this->_buf->data(this->_id), f._buf->data(f._id), 4);
		return *this;
	}
	uint32 operator =(uint32 val)
	{
		if( this->_buf )
		{
			uint8* buf = this->_buf->data(this->_id);
			buf[0] = uint8( (val & 0x000000FF)         );
			buf[1] = uint8( (val & 0x0000FF00) >> 0x08 );
			buf[2] = uint8( (val & 0x00FF0000) >> 0x10 );
			buf[3] = uint8( (val & 0xFF000000) >> 0x18 );
		}
		return val;
	}

private:
	///////////////////////////////////////////////////////////////////////////
	/// Returns 4.
	virtual size_t _initlength(size_t len)
	{
		return 4;
	}
};



/// Fixed-size string.
/// Warning: It's not nul-terminated if length equals SZ.
///
/// @param SZ Size of the string
template<size_t SZ>
class CFieldFixString : public AField
{
public:
	CFieldFixString()
	:	AField()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	operator const char*() const
	{
		return this->operator ()();
	}
	const char* operator ()() const
	{
		if( this->_buf )
			return (const char*)this->_buf->data(this->_id);
		return "";
	}

	CFieldFixString<SZ>& operator =(const CFieldFixString<SZ>& f)
	{
		if( this->_buf && f._buf )
			memcpy(this->_buf->data(this->_id), f._buf->data(f._id), SZ);
		return *this;
	}
	const char* operator =(const char* str)
	{
		if( this->_buf )
		{
			char* buf = (char*)this->_buf->data(this->_id);
			strncpy(buf, str, SZ);
			return buf;
		}
		return "";
	}

	/// Copies the string with the specified size to this field.
	void copy(const char* str, size_t sz)
	{
		if( this->_buf )
		{
			char* buf = (char*)this->_buf->data(this->_id);
			strncpy(buf, str, min(SZ, sz));
		}
	}

	/// Returns the length of this string.
	size_t length() const
	{
		return strnlen((const char*)this->_buf->data(this->_id), SZ);
	}

	/// Returns the capacity of this field.
	size_t capacity() const
	{
		return SZ;
	}

private:
	///////////////////////////////////////////////////////////////////////////
	/// Returns SZ.
	virtual size_t _initlength(size_t len)
	{
		return SZ;
	}
};



/// Dynamic-size nul-terminated string.
class CFieldCString : public AField
{
public:
	CFieldCString()
	:	AField()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	operator const char*() const
	{
		return this->operator ()();
	}
	const char* operator ()() const
	{
		if( this->_buf )
			return (const char*)this->_buf->data(this->_id);
		return "";
	}

	CFieldCString& operator =(const CFieldCString& f)
	{
		if( this->_buf && f._buf )
		{
			size_t len;
			if( !this->_buf->resize(this->_id, f._buf->length(f._id), len) )
			{
#if defined(DEBUG)
				if( len == 0 )//## ERROR this should never happen
					printf("[Debug] resize failed (len=0) at CFieldCString::operator=(const CFieldCString& f)\n");
#endif//DEBUG
				this->_buf->resize(this->_id, len, len);// resize to max instead
			}
			uint8* buf = this->_buf->data(this->_id);
			memcpy(buf, f._buf->data(f._id), len - 1);
			buf[len - 1] = 0;
		}
		return *this;
	}
	const char* operator =(const char* str)
	{
		if( this->_buf )
		{
			if( str )
				str = "";
			size_t len = strlen(str) + 1;
			if( !this->_buf->resize(this->_id, len, len) )
			{
#if defined(DEBUG)
				if( len == 0 )//## ERROR this should never happen
					printf("[Debug] resize failed (len=0) at CFieldCString::operator=(const char* str)\n");
#endif//DEBUG
				this->_buf->resize(this->_id, len, len);// resize to max instead
			}

			uint8* buf = this->_buf->data(this->_id);
			memcpy(buf, str, len - 1);
			buf[len - 1] = '\0';
			return (const char*)buf;
		}
		return "";
	}

	/// Copies the string with at most the specified size to this field.
	void copy(const char* str, size_t sz)
	{
		if( this->_buf )
		{
			if( str )
				str = "";
			size_t len = strlen(str);
			len = min(len, sz) + 1;
			if( !this->_buf->resize(this->_id, len, len) )
			{
#if defined(DEBUG)
				if( len == 0 )//## ERROR this should never happen
					printf("[Debug] resize failed (len=0) at CFieldCString::copy(const char* str, size_t sz)\n");
#endif//DEBUG
				this->_buf->resize(this->_id, len, len);// resize to max instead
			}
			char* buf = (char*)this->_buf->data(this->_id);
			memcpy(buf, str, len - 1);
			buf[len - 1] = '\0';
		}
	}

	/// Returns the length of this string.
	size_t length() const
	{
		if( this->_buf )
		{
			size_t len = this->_buf->length(this->_id);
			if( len > 0 )
				return len - 1;
		}
		return 0;
	}

	/// Returns the capacity of this string.
	size_t capacity() const
	{
		if( this->_buf )
		{
			size_t len = this->_buf->capacity(this->_id);
			if( len > 0 )
				return len - 1;
		}
		return 0;
	}

private:
	///////////////////////////////////////////////////////////////////////////
	/// Returns SZ.
	virtual size_t _initlength(size_t len)
	{
		if( this->_buf && this->_buf->data(this->_id) )// length based on data
		{
			char* buf = (char*)this->_buf->data(this->_id);
			size_t real_len = strnlen(buf, len);
			if( real_len == len )
				buf[real_len - 1] = '\0';
			else
				++real_len;
			return real_len;// string length + nul-terminator
		}
		else// minimum length
			return 1;// nul-terminator
	}
};



/// Array of a fixed number of sub-fields.
///
/// @param T Type of sub-field
/// @param SZ Number of sub-fields
template<class T,size_t SZ>
class CFieldFixArray : public ACompositeField<NFieldHandler::CFixPosSet<SZ> >
{
public:
	CFieldFixArray()
	:	ACompositeField<NFieldHandler::CFixPosSet<SZ> >()
	{}
	virtual ~CFieldFixArray()
	{}

public:
	///////////////////////////////////////////////////////////////////////////
	const T& operator [](size_t idx) const
	{
		return this->_arr[idx];
	}
	T& operator [](size_t idx)
	{
		return this->_arr[idx];
	}
	size_t length() const
	{
		return SZ;
	}
	size_t capacity() const
	{
		return SZ;
	}
private:
	/// Returns the sum of lengths of the sub-fields.
	virtual size_t _initlength(size_t len)
	{
		size_t i;
		size_t off = 0;
		for( i = 0; i < SZ; ++i )
		{
			this->_arr[i].Init(&this->_buf, i, off, len - min(off, len));
			off += this->_buf.length(i);
		}
		return off;
	}

private:
	///////////////////////////////////////////////////////////////////////////
	/// Array of fields
	T _arr[SZ];
};





///////////////////////////////////////////////////////////////////////////////
NAMESPACE_END(basics)
///////////////////////////////////////////////////////////////////////////////





#include "baseinet.h"
#include "basearray.h"

///////////////////////////////////////////////////////////////////////////////
// example code from caldon, adopted version provided by Hinoko
// I just put it here as reference; 
// as the implementation above is almost identical it might be not 
// necessary to have adoptions, but maybe the question with the 
// dynamic buffer can be answered by example here

namespace example_socket {


///////////////////////////////////////////////////////////////////////////////
/// base class for accessing default types.
/// empty to cause compile time errors
template <typename T>
struct subscript_typed
{};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for bytes.
template <>
struct subscript_typed<uint8>
{
	uint8* cWpp;
	subscript_typed(uint8* p) : cWpp(p)
	{}
	operator uint8() const
	{
		return (cWpp)?*cWpp:0;
	}
	const subscript_typed& operator=(uint8 v)
	{
		if(cWpp)
			*cWpp = v;
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for words.
/// transfer in little endian byte order
template <>
struct subscript_typed<uint16>
{
	uint8* cWpp;
	subscript_typed(uint8* p) : cWpp(p)
	{}
	
	operator uint16() const
	{
		if(cWpp)
		{
			return	(uint16)
					 ( ((uint16)(cWpp[0]))        )
					|( ((uint16)(cWpp[1])) << 0x08);
		}
		return 0;
	}
	const subscript_typed& operator=(uint16 v)
	{
		if(cWpp)
		{
			uint8*p=cWpp;
			*p++ = (unsigned char)(v); v>>=8;
			*p   = (unsigned char)(v);
		}
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for doublewords.
/// transfer in little endian byte order
template <>
struct subscript_typed<uint32>
{
	uint8* cWpp;
	subscript_typed(uint8* p) : cWpp(p)
	{}
	
	operator uint32() const
	{
		if(cWpp)
		{
			return	 ( ((uint32)(cWpp[0]))        )
					|( ((uint32)(cWpp[1])) << 0x08)
					|( ((uint32)(cWpp[2])) << 0x10)
					|( ((uint32)(cWpp[3])) << 0x18);
		}
		return 0;
	}
	const subscript_typed& operator=(uint32 v)
	{
		if(cWpp)
		{
			uint8*p=cWpp;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p   = (unsigned char)(v);
		}
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for quadwords.
/// transfer in little endian byte order
template <>
struct subscript_typed<uint64>
{
	uint8* cWpp;
	subscript_typed(uint8* p) : cWpp(p)
	{}
	operator uint64() const
	{
		if(cWpp)
		{
			return	 ( ((uint64)(cWpp[0]))        )
					|( ((uint64)(cWpp[1])) << 0x08)
					|( ((uint64)(cWpp[2])) << 0x10)
					|( ((uint64)(cWpp[3])) << 0x18)
					|( ((uint64)(cWpp[4])) << 0x20)
					|( ((uint64)(cWpp[5])) << 0x28)
					|( ((uint64)(cWpp[6])) << 0x30)
					|( ((uint64)(cWpp[7])) << 0x38);
		}
		return 0;
	}
	const subscript_typed& operator=(uint64 v)
	{
		if(cWpp)
		{
			uint8*p=cWpp;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p++ = (unsigned char)(v); v>>=8;
			*p   = (unsigned char)(v);
		}
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for cstring.
/// does never return null pointer
template <>
struct subscript_typed<const char*>
{
	uint8* cWpp;
	size_t sz;
	subscript_typed(uint8* p, size_t s) : cWpp(p), sz(s)
	{}
	operator const char*() const
	{
		if(cWpp&&sz)
		{
			cWpp[sz-1]=0;// force eos
			return	(const char*)(cWpp);
		}
		return "";
	}
	const subscript_typed& operator=(const char* v)
	{
		if(cWpp&&v&&sz)
		{
			memcpy(cWpp, v, sz);
			cWpp[sz-1]=0;
		}
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for byte buffers.
/// returns null pointer when accessing out-of-bound
template <>
struct subscript_typed<const unsigned char*>
{
	uint8* cWpp;
	size_t sz;
	subscript_typed(uint8* p, size_t s) : cWpp(p), sz(s)
	{}
	operator const unsigned char*() const
	{
		return (const unsigned char*)cWpp;
	}
	const subscript_typed& operator=(const char* v)
	{
		if(cWpp&&v&&sz)
			memcpy(cWpp, v, sz);
		return *this;
	}
};

///////////////////////////////////////////////////////////////////////////////
/// type access specialisation for basics::ipaddess.
/// transfer ipaddresses as big endian (network byte order)
template <>
struct subscript_typed<basics::ipaddress>
{
	uint8* cWpp;
	subscript_typed(uint8* p) : cWpp(p)
	{}
	operator basics::ipaddress() const
	{
		if(cWpp)
		{
			return	 ( ((unsigned long)(cWpp[3]))        )
					|( ((unsigned long)(cWpp[2])) << 0x08)
					|( ((unsigned long)(cWpp[1])) << 0x10)
					|( ((unsigned long)(cWpp[0])) << 0x18);
		}
		return basics::ipaddress();
	}
	const subscript_typed& operator=(const basics::ipaddress& v)
	{
		if(cWpp)
		{
			cWpp[3] = (unsigned char)((v & 0x000000FF)          );
			cWpp[2] = (unsigned char)((v & 0x0000FF00)  >> 0x08 );
			cWpp[1] = (unsigned char)((v & 0x00FF0000)  >> 0x10 );
			cWpp[0] = (unsigned char)((v & 0xFF000000)  >> 0x18 );
		}
		return *this;
	}
};


///////////////////////////////////////////////////////////////////////////////
/// abstract subscript type.
/// handles type specification for accessing the parent object 
/// by aquiring a piece of buffer space inside the parent
template <typename Parent>
struct subscript
{
	Parent& parent;
	size_t  index;
	subscript(Parent& p, size_t inx) : parent(p),index(inx)
	{}
	~subscript()
	{}
	subscript_typed<uint8> u8()
	{
		return subscript_typed<uint8>(parent.aquire(index,1));
	}
	subscript_typed<uint16> u16()
	{
		return subscript_typed<uint16>(parent.aquire(index,2));
	}
	subscript_typed<uint32> u32()
	{
		return subscript_typed<uint32>(parent.aquire(index,4));
	}
	subscript_typed<uint64> u64()
	{
		return subscript_typed<uint64>(parent.aquire(index,8));
	}
	subscript_typed<const char*> string(size_t sz)
	{
		return subscript_typed<const char*>(parent.aquire(index,sz),sz);
	}
	subscript_typed<const unsigned char*> blob(size_t sz)
	{
		return subscript_typed<const unsigned char*>(parent.aquire(index,sz),sz);
	}
	subscript_typed<basics::ipaddress> ip()
	{
		return subscript_typed<basics::ipaddress>(parent.aquire(index,4));
	}
};


///////////////////////////////////////////////////////////////////////////////
/// base packet.
class packet
{
protected:
	friend struct subscript<packet>;
	///////////////////////////////////////////////////////////////////////////
	/// aquire a block of sz bytes starting from pos inside the buffer.
	virtual uint8* aquire(size_t pos, size_t sz)=0;
public:
	///////////////////////////////////////////////////////////////////////////
	/// destructor.
	virtual ~packet()
	{}
	///////////////////////////////////////////////////////////////////////////
	/// access to typed elements
	subscript<packet> operator[](unsigned int inx)
	{
		return subscript<packet>(*this, inx);
	}
	///////////////////////////////////////////////////////////////////////////
	/// Length of the packet.
	virtual size_t length() const=0;
	///////////////////////////////////////////////////////////////////////////
	/// Data of the packet.
	virtual const uint8* operator()() const=0;
};


///////////////////////////////////////////////////////////////////////////////
/// fixed size packet.
template<size_t SZ>
class packet_fixed : public packet
{
protected:
	uint8 data[SZ];	///< the buffer
	///////////////////////////////////////////////////////////////////////////
	/// aquire a block of sz bytes starting from pos inside the buffer.
	/// return NULL when accessing out-of-range, overloadable
	virtual uint8* aquire(size_t pos, size_t sz)
	{
		return (pos+sz<=this->length())?const_cast<uint8*>(this->operator()()+pos):NULL;
	}
public:
	///////////////////////////////////////////////////////////////////////////
	virtual ~packet_fixed()
	{}
	///////////////////////////////////////////////////////////////////////////
	/// Length of the packet
	virtual size_t length() const			{ return SZ; }
	///////////////////////////////////////////////////////////////////////////
	/// Data of the packet
	virtual const uint8* operator()() const	{ return this->data; }
};



///////////////////////////////////////////////////////////////////////////////
/// dynamic size packet.
class packet_dynamic : public packet
{
protected:
	basics::vector<uint8> data;	///< the buffer
	///////////////////////////////////////////////////////////////////////////
	/// aquire a block of sz bytes starting from pos inside the buffer.
	/// resize the vector when accessing out-of-range
	virtual uint8* aquire(size_t pos, size_t sz)
	{
		if( pos+sz>this->data.size() )
			this->data.resize(pos+sz);
		return this->data.begin()+pos;
	}
public:
	///////////////////////////////////////////////////////////////////////////
	virtual ~packet_dynamic()
	{}

	///////////////////////////////////////////////////////////////////////////
	/// Length of the packet
	virtual size_t length() const			{ return this->data.size(); }
	///////////////////////////////////////////////////////////////////////////
	/// Data of the packet
	virtual const uint8* operator()() const	{ return this->data.begin(); }
};





inline void send(const packet& pk)
{
	// would do sending here in a real implementation
	// pk() and pk.size() give access to the necessary internals
}


} // end namespace example_socket
///////////////////////////////////////////////////////////////////////////////



namespace example_code {

// some packet
struct my_packet12345
	: public example_socket::packet_fixed<21>	// derive from fixed size packet as we might know that size is fixed
{
public:
	my_packet12345(int a, int b, int c)
	{
		// does whatever to put a,b,c into correct positions of this->data
	}
};
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4189) // variable declared but not used
#endif
inline void usage_example()
{
	// create temporary and send it
	example_socket::send( my_packet12345(1,2,3) );

	// create explicit var
	my_packet12345 pk(1,2,3);
	// and send it
	example_socket::send( pk );


	// free read/write from packets
	{
		example_socket::packet_dynamic pk1;

		pk1[5].u16() = 5;						// put 5 as ushort to position 5
		uint64 ss  = pk1[5].u32();				// read a uint32 from position 5, then cast it to uint64
		char ss1 = pk1[5].u32();				// read a byte from position 5, then cast it to char
		pk1[10].string(10) = "hallo";			// write a string with 10 chars max to position 10
		const char* str = pk1[12].string(5);	// read a string with 5 chars max from position 12
		// prevent unused variable warning
		ss1+=ss++;
		str = " ";
		str++;
	}
	{
		example_socket::packet_fixed<12> pk1;

		pk1[5].u16() = 5;						// put 5 as ushort to position 5
		uint64 ss  = pk1[5].u32();				// read a uint32 from position 5, then cast it to uint64
		char ss1 = pk1[5].u32();				// read a byte from position 5, then cast it to char
		pk1[10].string(10) = "hallo";			// write a string with 10 chars max to position 10
		const char* str = pk1[12].string(5);	// read a string with 5 chars max from position 12
		// prevent unused variable warning
		ss1+=ss++;
		str = " ";
		str++;
	}
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif


} // end namespace example_code


///////////////////////////////////////////////////////////////////////////////

#endif//__BASEPACKET_H__


