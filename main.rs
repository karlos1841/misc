use std::io::prelude::*;
use std::io::Write;
use std::fs::File;
use std::fmt;
use std::net::{TcpListener, TcpStream};
//use std::time::Duration;
//use std::thread;
use std::io::BufReader;
use std::process::Command;

fn main()
{
    let mut f = File::open("index.html").unwrap();
    let mut index_buffer = String::new();
    f.read_to_string(&mut index_buffer).unwrap();

    let mut f2 = File::open("index2.html").unwrap();
    let mut index2_buffer = String::new();
    f2.read_to_string(&mut index2_buffer).unwrap();


    let listener = TcpListener::bind("127.0.0.1:8080").unwrap();
    loop
    {
        match listener.accept()
        {
            Ok((stream, addr)) => 
            {
                let mut address = String::new();
                fmt::write(&mut address, format_args!("{}", addr)).unwrap();
                println!("New client: {}", address);
                //let index_buffer_clone = index_buffer.clone();
                //let index2_buffer_clone = index2_buffer.clone();
                //thread::spawn(|| {
                    let data: String = handle_connection(stream, &index_buffer, &index2_buffer);
                    send_mail(&address, &data, &"mail");
                //});
            },
            Err(e) => println!("could not get a client: {}", e)
        }
    }
}

fn send_mail(address: &String, data: &String, recip: &str)
{
    if !data.is_empty()
    {
        let mut formatted_output = String::new();
        fmt::write(&mut formatted_output, format_args!("echo \"Adres klienta: {}\n{}\n\"|mail -s formularz {}", address, data, recip)).unwrap();
        let mut mail_cmd = Command::new("/bin/sh");
        mail_cmd.arg("-c")
                .arg(&formatted_output)
                .spawn()
                .unwrap();
    }
}

fn handle_connection(stream: TcpStream, index: &String, index2: &String) -> String
{
    // panic if cannot set stream as nonblocking
    //stream.set_nonblocking(true).unwrap();

    // 1 second timeout
    //let timeout: Option<Duration> = Some(Duration::new(1, 0));
    //stream.set_read_timeout(timeout).unwrap();

    let mut client_response_header = String::new();
    let mut stream_buf = BufReader::new(&stream);
    loop
    {
        let mut tmp = String::new();
        stream_buf.read_line(&mut tmp).unwrap();
        client_response_header.push_str(&tmp);
        if tmp == "\r\n"{break;}
    }
    // client_response_header contains http header now
    println!("{}", client_response_header);

    let mut client_response_data = String::new();
    if client_response_header.contains("Content-Length: ")
    {
        let content_length: Vec<&str> = client_response_header.rsplit("Content-Length: ").collect();
        let content_length: Vec<&str> = content_length[0].split("\r\n").collect();
        let client_response_data_length: usize = content_length[0].parse().unwrap();
        let mut data_buffer: Vec<u8> = vec![0; client_response_data_length];
        stream_buf.read_exact(&mut data_buffer).unwrap();
        match String::from_utf8(data_buffer)
        {
            Ok(s) => {client_response_data.push_str(&s);},
            Err(_) => {}
        }
    }
    // client_response_data contains data after http header
    println!("{}", client_response_data);

    client_response_data = client_response_data.replace("&", "\n");
    let result: Vec<&str> = client_response_data.lines().collect();
    //println!("{:?}", result.last());
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

    return client_response_data.clone();
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