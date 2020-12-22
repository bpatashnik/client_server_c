# Initialization

TCP: Upon socket creation, the TCP thread on the server process will send four initialization messages to the TCP thread on the client process.
1. file_size - The total size of the file about to be transferred
2. num_full_chunks - The number of complete, 4096-byte chunks to be sent
3. total_chunks - The total number of chunks (complete and incomplete) to be sent
4. bytes_left - The number of bytes to be sent in the incomplete chunk

Based upon the number of chunks and bytes that the client now expects to receive, memory will be allocated to hold the coming file chunks.

UDP: After socket creation, the UDP thread on the client process will send a message to the UDP thread on the server process indicating that it is ready to receive chunks.

# Done Sending

Once the server has left the chunk sending loop, the TCP thread will send the All_Sent message to the TCP thread of the client. The client will then send the AckMessage, containing an array of bytes to track which chunks have been successfully received.

# Acknowledgment

Upon receiving the AckMessage from the client, the server will scan through the array to find the first missing chunk. Once found, the TCP thread will pause and the UDP thread will begin again. The UDP thread will begin a for-loop starting from the first missing chunk and perform a conditional check on all subsequent chunks. If the AckMessage indicates that the client has not received a chunk (i.e. a ‘0’ byte), the server will send that chunk. If the client has received the chunk (i.e. a ‘1’ byte), the server will move onto the next chunk.

# Transfer Completed

Once the AckMessage from the client contains all ‘1’s, the TCP thread on the server process will send a message to the client indicating that the entire file has been transferred. Both processes will then exit their UDP and TCP threads, ending both programs.
