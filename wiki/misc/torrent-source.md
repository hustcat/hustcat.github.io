---
layout: post
title: Notes for torrent source code
date: 2016-07-25 16:00:30
categories: P2P
tags: Torrent
excerpt: Notes for torrent source code
---

### GotInfo

```
// Returns a channel that is closed when the info (.Info()) for the torrent
// has become available.
func (t *Torrent) GotInfo() <-chan struct{} {
	t.cl.mu.Lock()
	defer t.cl.mu.Unlock()
	return t.gotMetainfo.C()
}
```

notify:

```
// Called when metadata for a torrent becomes available.
func (t *Torrent) setInfoBytes(b []byte) error {
...
	t.info = ie
	t.cl.event.Broadcast()
	t.gotMetainfo.Set()
```


对于torrent文件，metadata直接从torrent文件读取:

```
func (cl *Client) AddTorrentSpec(spec *TorrentSpec) (t *Torrent, new bool, err error) {
	t, new = cl.AddTorrentInfoHash(spec.InfoHash)
	if spec.DisplayName != "" {
		t.SetDisplayName(spec.DisplayName)
	}
	if spec.Info != nil {
		err = t.SetInfoBytes(spec.Info.Bytes)
		if err != nil {
			return
		}
...
```

对于magnet link:

```
func (t *Torrent) maybeMetadataCompleted() {
	if t.haveInfo() {
		// Nothing to do.
		return
	}
	if !t.haveAllMetadataPieces() {
		// Don't have enough metadata pieces.
		return
	}
	// TODO(anacrolix): If this fails, I think something harsher should be
	// done.
	err := t.setInfoBytes(t.metadataBytes)
...



// Process incoming ut_metadata message.
func (cl *Client) gotMetadataExtensionMsg(payload []byte, t *Torrent, c *connection) (err error) {
	var d map[string]int
	err = bencode.Unmarshal(payload, &d)
	if err != nil {
		err = fmt.Errorf("error unmarshalling payload: %s: %q", err, payload)
		return
	}
	msgType, ok := d["msg_type"]
	if !ok {
		err = errors.New("missing msg_type field")
		return
	}
	piece := d["piece"]
	switch msgType {
	case pp.DataMetadataExtensionMsgType:
		if !c.requestedMetadataPiece(piece) {
			err = fmt.Errorf("got unexpected piece %d", piece)
			return
		}
		c.metadataRequests[piece] = false
		begin := len(payload) - metadataPieceSize(d["total_size"], piece)
		if begin < 0 || begin >= len(payload) {
			err = fmt.Errorf("data has bad offset in payload: %d", begin)
			return
		}
		t.saveMetadataPiece(piece, payload[begin:])
		c.UsefulChunksReceived++
		c.lastUsefulChunkReceived = time.Now()
		t.maybeMetadataCompleted()
```

send HandshakeExtendedID:

```
func (cl *Client) sendInitialMessages(conn *connection, torrent *Torrent) {
	if conn.PeerExtensionBytes.SupportsExtended() && cl.extensionBytes.SupportsExtended() {
		conn.Post(pp.Message{
			Type:       pp.Extended,
			ExtendedID: pp.HandshakeExtendedID,
...
```

recv HandshakeExtendedID:

```
```

### (t *Torrent) DownloadAll()