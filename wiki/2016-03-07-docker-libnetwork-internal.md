


# docker network connect

## --ip

api/client/network.go

```go
// CmdNetworkConnect connects a container to a network
//
// Usage: docker network connect [OPTIONS] <NETWORK> <CONTAINER>
func (cli *DockerCli) CmdNetworkConnect(args ...string) error {
...
	flIPAddress := cmd.String([]string{"-ip"}, "", "IP Address")

	epConfig := &network.EndpointSettings{
		IPAMConfig: &network.EndpointIPAMConfig{
			IPv4Address: *flIPAddress,
			IPv6Address: *flIPv6Address,
		},
		Links:   flLinks.GetAll(),
		Aliases: flAliases.GetAll(),
	}
```

api/server/router/network/network_routes.go

```go
func (n *networkRouter) postNetworkConnect(ctx context.Context, w http.ResponseWriter, r *http.Request, vars map[string]string) error {
...
    nw, err := n.backend.FindNetwork(vars["id"])
    if err != nil {
        return err
    }

    return n.backend.ConnectContainerToNetwork(connect.Container, nw.Name(), connect.EndpointConfig)
}
```

daemon/container_operations_unix.go

```go
func (daemon *Daemon) connectToNetwork(container *container.Container, idOrName string, endpointConfig *networktypes.EndpointSettings, updateSettings bool) (err error) {
...
	controller := daemon.netController
	/// options
	sb := daemon.getNetworkSandbox(container)
	createOptions, err := container.BuildCreateEndpointOptions(n, endpointConfig, sb)
	if err != nil {
		return err
	}
```

container/container_unix.go

```go
// BuildCreateEndpointOptions builds endpoint options from a given network.
func (container *Container) BuildCreateEndpointOptions(n libnetwork.Network, epConfig *network.EndpointSettings, sb libnetwork.Sandbox) ([]libnetwork.EndpointOption, error) {
...
	if epConfig != nil {
		ipam := epConfig.IPAMConfig
		if ipam != nil && (ipam.IPv4Address != "" || ipam.IPv6Address != "") {
			createOptions = append(createOptions,
				libnetwork.CreateOptionIpam(net.ParseIP(ipam.IPv4Address), net.ParseIP(ipam.IPv6Address), nil))
		}
```

libnetwork/endpoint.go

```go
// CreateOptionIpam function returns an option setter for the ipam configuration for this endpoint
func CreateOptionIpam(ipV4, ipV6 net.IP, ipamOptions map[string]string) EndpointOption {
	return func(ep *endpoint) {
		ep.prefAddress = ipV4
		ep.prefAddressV6 = ipV6
		ep.ipamOptions = ipamOptions
	}
}
```

https://github.com/docker/libnetwork/blob/master/docs/ipam.md
https://github.com/docker/libnetwork/pull/970