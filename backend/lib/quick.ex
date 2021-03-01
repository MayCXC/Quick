defmodule Quick do
  @moduledoc """
  Documentation for `Quick`.
  """

  @doc """
  TCP server with WebSocket mirror.
  """
  require Logger

  use Application

  @impl true
  def start(_type, _args) do
    Supervisor.start_link(
      [
        { Task.Supervisor, name: Quick.TaskSupervisor },
        Supervisor.child_spec({Task, fn -> accept(4040) end}, restart: :permanent),
        Plug.Cowboy.child_spec(
          scheme: :http,
          plug: nil,
          options: [port: 8080, dispatch: dispatch()]
        ),
        Registry.child_spec(
          keys: :duplicate,
          name: Registry.SocketHandler
        )
      ],
      [
        strategy: :one_for_one,
        name: Quick.Supervisor
      ]
    )
  end

  def accept(port) do
    # The options below mean:
    #
    # 1. `:binary` - receives data as binaries (instead of lists)
    # 2. `packet: :line` - receives data line by line
    # 3. `active: false` - blocks on `:gen_tcp.recv/2` until data is available
    # 4. `reuseaddr: true` - allows us to reuse the address if the listener crashes
    #
    {:ok, socket} = :gen_tcp.listen port, [
      mode: :binary,
      packet: :line,
      active: false,
      reuseaddr: true
    ]
    Logger.info "Accepting connections on port #{port}"
    loop_acceptor socket
  end

  defp loop_acceptor(socket) do
    {:ok, client} = :gen_tcp.accept socket
    client |> Port.info |> Logger.info
    {:ok, pid} = Task.Supervisor.start_child Quick.TaskSupervisor, fn -> serve client end
    :ok = :gen_tcp.controlling_process client, pid
    loop_acceptor socket
  end

  defp serve(socket) do
    socket
    |> read_line
#   |> write_line(socket)
    |> write_line_ws("/socket")

    serve socket
  end

  defp read_line(socket) do
    {:ok, data} = :gen_tcp.recv socket, 0
    Logger.info data
    data
  end

# defp write_line(line, socket) do
#   :gen_tcp.send socket, line
# end
  defp write_line_ws(line, ws) do
    Logger.info "Registry: #{inspect(Registry.lookup(Registry.SocketHandler, ws))}"
    Registry.dispatch Registry.SocketHandler, ws, fn(entries) ->
      for {pid, _} <- entries do
        Process.send pid, line, []
      end
    end
  end

  defp dispatch() do
    [
      {
        :_,
        [
          {"/socket", Quick.SocketHandler, %{}},
          {:_, Plug.Cowboy.Handler, {Quick.Router, []}}
        ]
      }
    ]
  end
end
