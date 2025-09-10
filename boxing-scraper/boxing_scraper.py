import requests
from bs4 import BeautifulSoup
import time
from urllib.parse import urljoin
import re


def fetchWikipediaPage(url):
    # Add headers to avoid being blocked
    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
    }
    try:
        print(f"Fetching Wikipedia page: {url}")
        response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        
        # Parse through the HTML
        soup = BeautifulSoup(response.text, 'html.parser')
        return soup
    except Exception as e:
        print(f"Error fetching Wikipedia page: {e}")
        return None

#  Extract fighter Wikipedia links from a seed page
def extractFighterLinks(seedUrl):
    soup = fetchWikipediaPage(seedUrl)
    if not soup:
        return []
    
    
    # Avoid duplicate links
    fighterLinks = set()
    baseUrl = "https://en.wikipedia.org"
    # Extract the links from the page
    links = soup.find_all('a', href=True)
    for link in links:
        href = link.get('href')
        if not href or not href.startswith('/wiki/'):
            continue
        #Extract full url
        fullUrl = urljoin(baseUrl, href)
        if isLikelyFighterPage(fullUrl):
            fighterLinks.add(fullUrl)
    # Keep now for debugging
    print(f"Found {len(fighterLinks)} fighter links")
    return list(fighterLinks)


if __name__ == "__main__":
    # Test with Mike Tyson the GOAT frfrfr
    testUrl = "https://en.wikipedia.org/wiki/Mike_Tyson"
    soup = fetchWikipediaPage(testUrl)
    if (soup):
        title = soup.find('h1', class_='firstHeading')
        if (title):
            print(f"Sucessfully fetched page: {title.get_text()}")
        else:
            print("Fetched page but couldn't find the title")
    else:
        print("Failed to fetch Wikipedia page")