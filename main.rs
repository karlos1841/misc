use std::io::prelude::*;
use std::io::Write;
use std::fs::File;
use std::fmt;
use std::net::{TcpListener, TcpStream};
use std::time::Duration;
use std::thread;
use std::io::BufReader;

fn main()
{
    let mut f = File::open("/home/karol/Downloads/index.html").unwrap();
    let mut index_buffer = String::new();
    f.read_to_string(&mut index_buffer).unwrap();

    let mut f2 = File::open("/home/karol/Downloads/index2.html").unwrap();
    let mut index2_buffer = String::new();
    f2.read_to_string(&mut index2_buffer).unwrap();


    let listener = TcpListener::bind("127.0.0.1:8080").unwrap();
    loop
    {
        match listener.accept()
        {
            Ok((stream, addr)) => 
            {
                println!("New client: {}", addr);
                let index_buffer_clone = index_buffer.clone();
                let index2_buffer_clone = index2_buffer.clone();
                thread::spawn(|| {
                    handle_connection(stream, index_buffer_clone, index2_buffer_clone);
                });
            },
            Err(e) => println!("could not get a client: {}", e)
        }
    }
}

fn handle_connection(stream: TcpStream, index: String, index2: String)
{
    // panic if cannot set stream as nonblocking
    //stream.set_nonblocking(true).unwrap();

    // gathering requests from client
    // 1 second timeout
    //let timeout: Option<Duration> = Some(Duration::new(1, 0));
    //stream.set_read_timeout(timeout).unwrap();

    let mut client_response = String::new();
    let mut stream_buf = BufReader::new(&stream);
    loop
    {
        let mut tmp = String::new();
        stream_buf.read_line(&mut tmp).unwrap();
        client_response.push_str(&tmp);
        if tmp == "\r\n"
        {
            println!("{}", client_response);
            break;
        }
    }
    println!("{}", client_response);
    let data_length: u32 = 0;
    let mut data_str: &str;
    if client_response.contains("Content-Length: ")
    {
        let content_length: Vec<&str> = client_response.rsplit("Content-Length: ").collect();
        let content_length: Vec<&str> = content_length[0].split("\r\n").collect();
        let data_length: usize = content_length[0].parse().unwrap();
        let mut data_buffer = vec![0; data_length];
        stream_buf.read_exact(&mut data_buffer).unwrap();
        match std::str::from_utf8(&data_buffer)
        {
            Ok(s) => {},//{data_str = s;},
            Err(_) => {}
        }
    }
    println!("{}", data_length);
    println!("{:?}", data_str);

    // take first not GET client request(POST) and split by \r\n\r\n so that data is the last element
    let data: Vec<&str> = client_response.split("GET").collect();
    let data: Vec<&str> = data[0].split("\r\n\r\n").collect();

    let mut result = String::new();
    if data.last() != Some(&"")
    {
        match data.last()
        {
            Some(ref x) => {
                result.push_str(x);
            },
            None => {}
        }
    }
    result = result.replace("&", "\n");
    let result: Vec<&str> = result.lines().collect();
    println!("{:?}", result.last());
    if result.last() == Some(&"dalej=Dalej")
    {
        // send next page
        send_response(&stream, &index2);
    }
    else
    {
        // send first page to client
        send_response(&stream, &index);
    }
}

fn send_response(mut stream: &TcpStream, data: &String)
{
    let mut server_response = String::new();
    match fmt::write(&mut server_response, format_args!(
    "\
    HTTP/1.1 200 OK\r\n\
    Server: rust\r\n\
    Accept-Ranges: bytes\r\n\
    Content-Length: {}\r\n\
    Content-Type: text/html\r\n\r\n\
    {}", data.len(), data))
    {
        Ok(_) => {},
        Err(_) => println!("Error occurred while trying to write in response String")
    }
    match stream.write(server_response.as_bytes())
    {
        Ok(_) => {},//println!("Sent {} bytes", n),
        Err(e) => {println!("Write error: {}", e); return;}
    }
    match stream.flush()
    {
        Ok(_) => {},//println!("Flushed stream properly"),
        Err(e) => {println!("Could not flush all bytes: {}", e); return;}
    }

    //println!("{}", server_response);
}