defmodule Quick.Router do
  use Plug.Router

  plug Plug.Logger

  plug Plug.Static,
    at: "/",
    from: :quick,
    only: ["favicon.ico", "panel.html"]

  plug :match
  plug :dispatch

  get "/ping" do
    send_resp(conn, 200, "pong")
  end
end
