#include <string.h>

#include "wren_utils.h"
#include "wren_vm.h"

DEFINE_BUFFER(Byte, uint8_t);
DEFINE_BUFFER(Int, int);
DEFINE_BUFFER(String, ObjString*);

// FNV-1a, over the name's bytes.
static uint32_t hashName(const char* name, size_t length)
{
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; i++)
  {
    hash ^= (uint8_t)name[i];
    hash *= 16777619;
  }
  return hash;
}

// Puts [symbol] in the index unless an earlier symbol has the same name: the
// first one keeps it, as a scan from the start would find it.
static void indexSymbol(SymbolTable* symbols, int symbol)
{
  ObjString* name = symbols->data[symbol];
  uint32_t mask = (uint32_t)symbols->slotCapacity - 1;
  uint32_t slot = hashName(name->value, name->length) & mask;
  while (symbols->slots[slot] != 0)
  {
    ObjString* other = symbols->data[symbols->slots[slot] - 1];
    if (other->length == name->length &&
        memcmp(other->value, name->value, name->length) == 0)
    {
      return;
    }
    slot = (slot + 1) & mask;
  }
  symbols->slots[slot] = symbol + 1;
}

void wrenSymbolTableInit(SymbolTable* symbols)
{
  symbols->data = NULL;
  symbols->count = 0;
  symbols->capacity = 0;
  symbols->slots = NULL;
  symbols->slotCapacity = 0;
}

void wrenSymbolTableClear(WrenVM* vm, SymbolTable* symbols)
{
  wrenReallocate(vm, symbols->data, symbols->capacity * sizeof(ObjString*), 0);
  wrenReallocate(vm, symbols->slots, symbols->slotCapacity * sizeof(int), 0);
  wrenSymbolTableInit(symbols);
}

int wrenSymbolTableAdd(WrenVM* vm, SymbolTable* symbols,
                       const char* name, size_t length)
{
  ObjString* symbol = AS_STRING(wrenNewStringLength(vm, name, length));

  wrenPushRoot(vm, &symbol->obj);
  if (symbols->capacity < symbols->count + 1)
  {
    int capacity = wrenPowerOf2Ceil(symbols->count + 1);
    symbols->data = (ObjString**)wrenReallocate(vm, symbols->data,
        symbols->capacity * sizeof(ObjString*), capacity * sizeof(ObjString*));
    symbols->capacity = capacity;
  }
  symbols->data[symbols->count++] = symbol;

  if (symbols->slotCapacity < symbols->count * 2)
  {
    int slotCapacity = symbols->slotCapacity == 0 ? 16 : symbols->slotCapacity;
    while (slotCapacity < symbols->count * 2) slotCapacity *= 2;
    wrenReallocate(vm, symbols->slots, symbols->slotCapacity * sizeof(int), 0);
    symbols->slots = (int*)wrenReallocate(vm, NULL, 0,
                                          slotCapacity * sizeof(int));
    memset(symbols->slots, 0, slotCapacity * sizeof(int));
    symbols->slotCapacity = slotCapacity;
    for (int i = 0; i < symbols->count; i++) indexSymbol(symbols, i);
  }
  else
  {
    indexSymbol(symbols, symbols->count - 1);
  }
  wrenPopRoot(vm);

  return symbols->count - 1;
}

int wrenSymbolTableEnsure(WrenVM* vm, SymbolTable* symbols,
                          const char* name, size_t length)
{
  // See if the symbol is already defined.
  int existing = wrenSymbolTableFind(symbols, name, length);
  if (existing != -1) return existing;

  // New symbol, so add it.
  return wrenSymbolTableAdd(vm, symbols, name, length);
}

int wrenSymbolTableFind(const SymbolTable* symbols,
                        const char* name, size_t length)
{
  if (symbols->slotCapacity == 0) return -1;
  uint32_t mask = (uint32_t)symbols->slotCapacity - 1;
  uint32_t slot = hashName(name, length) & mask;
  while (symbols->slots[slot] != 0)
  {
    int symbol = symbols->slots[slot] - 1;
    if (wrenStringEqualsCString(symbols->data[symbol], name, length))
    {
      return symbol;
    }
    slot = (slot + 1) & mask;
  }
  return -1;
}

void wrenBlackenSymbolTable(WrenVM* vm, SymbolTable* symbolTable)
{
  for (int i = 0; i < symbolTable->count; i++)
  {
    wrenGrayObj(vm, &symbolTable->data[i]->obj);
  }
  
  // Keep track of how much memory is still in use.
  vm->bytesAllocated += symbolTable->capacity * sizeof(*symbolTable->data);
}

int wrenUtf8EncodeNumBytes(int value)
{
  ASSERT(value >= 0, "Cannot encode a negative value.");
  
  if (value <= 0x7f) return 1;
  if (value <= 0x7ff) return 2;
  if (value <= 0xffff) return 3;
  if (value <= 0x10ffff) return 4;
  return 0;
}

int wrenUtf8Encode(int value, uint8_t* bytes)
{
  if (value <= 0x7f)
  {
    // Single byte (i.e. fits in ASCII).
    *bytes = value & 0x7f;
    return 1;
  }
  else if (value <= 0x7ff)
  {
    // Two byte sequence: 110xxxxx 10xxxxxx.
    *bytes = 0xc0 | ((value & 0x7c0) >> 6);
    bytes++;
    *bytes = 0x80 | (value & 0x3f);
    return 2;
  }
  else if (value <= 0xffff)
  {
    // Three byte sequence: 1110xxxx 10xxxxxx 10xxxxxx.
    *bytes = 0xe0 | ((value & 0xf000) >> 12);
    bytes++;
    *bytes = 0x80 | ((value & 0xfc0) >> 6);
    bytes++;
    *bytes = 0x80 | (value & 0x3f);
    return 3;
  }
  else if (value <= 0x10ffff)
  {
    // Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
    *bytes = 0xf0 | ((value & 0x1c0000) >> 18);
    bytes++;
    *bytes = 0x80 | ((value & 0x3f000) >> 12);
    bytes++;
    *bytes = 0x80 | ((value & 0xfc0) >> 6);
    bytes++;
    *bytes = 0x80 | (value & 0x3f);
    return 4;
  }

  // Invalid Unicode value. See: http://tools.ietf.org/html/rfc3629
  UNREACHABLE();
  return 0;
}

int wrenUtf8Decode(const uint8_t* bytes, uint32_t length)
{
  // Single byte (i.e. fits in ASCII).
  if (*bytes <= 0x7f) return *bytes;

  int value;
  uint32_t remainingBytes;
  if ((*bytes & 0xe0) == 0xc0)
  {
    // Two byte sequence: 110xxxxx 10xxxxxx.
    value = *bytes & 0x1f;
    remainingBytes = 1;
  }
  else if ((*bytes & 0xf0) == 0xe0)
  {
    // Three byte sequence: 1110xxxx	 10xxxxxx 10xxxxxx.
    value = *bytes & 0x0f;
    remainingBytes = 2;
  }
  else if ((*bytes & 0xf8) == 0xf0)
  {
    // Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
    value = *bytes & 0x07;
    remainingBytes = 3;
  }
  else
  {
    // Invalid UTF-8 sequence.
    return -1;
  }

  // Don't read past the end of the buffer on truncated UTF-8.
  if (remainingBytes > length - 1) return -1;

  while (remainingBytes > 0)
  {
    bytes++;
    remainingBytes--;

    // Remaining bytes must be of form 10xxxxxx.
    if ((*bytes & 0xc0) != 0x80) return -1;

    value = value << 6 | (*bytes & 0x3f);
  }

  return value;
}

int wrenUtf8DecodeNumBytes(uint8_t byte)
{
  // If the byte starts with 10xxxxx, it's the middle of a UTF-8 sequence, so
  // don't count it at all.
  if ((byte & 0xc0) == 0x80) return 0;
  
  // The first byte's high bits tell us how many bytes are in the UTF-8
  // sequence.
  if ((byte & 0xf8) == 0xf0) return 4;
  if ((byte & 0xf0) == 0xe0) return 3;
  if ((byte & 0xe0) == 0xc0) return 2;
  return 1;
}

// From: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2Float
int wrenPowerOf2Ceil(int n)
{
  n--;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n++;
  
  return n;
}

uint32_t wrenValidateIndex(uint32_t count, int64_t value)
{
  // Negative indices count from the end.
  if (value < 0) value = count + value;

  // Check bounds.
  if (value >= 0 && value < count) return (uint32_t)value;

  return UINT32_MAX;
}
